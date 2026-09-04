/* cerco — server internal structures. NOT part of the public API.
 *
 * Ownership model:
 *   - The event loop thread owns all uv handles, connections and arenas.
 *   - A connection owns one request arena reused across its requests.
 *   - A request context (cerco_req) lives in that arena and is borrowed by a
 *     worker while in flight. Workers only read request data and write the
 *     response into the arena; they never touch sockets or libuv handles.
 *   - If the connection dies while a worker holds a request, the event loop
 *     marks the conn dead and defers destruction until the worker completes;
 *     the completion is then discarded.
 */
#ifndef CERCO_INTERNAL_H
#define CERCO_INTERNAL_H

#include <uv.h>
#include <llhttp.h>

#include "cerco.h"
#include "arena.h"
#include "sb.h"
#include "str.h"
#include "wire.h"
#include "sha256.h"

#include <stdatomic.h>

#define CERCO_MAX_HEADERS 100
#define CERCO_MAX_HEADER_BYTES (32 * 1024)
#define CERCO_MAX_URL 8 * 1024
#define CERCO_MAX_NAME 256
#define CERCO_MAX_VALUE 8 * 1024
#define CERCO_READ_BUF 16 * 1024
#define CERCO_MAX_WRITE_PENDING (4 * 1024 * 1024)
#define CERCO_SSE_MAX_PENDING 64 * 1024

typedef struct cerco_conn cerco_conn;
typedef struct cerco_server cerco_server;

typedef struct cerco_resp_hdr {
  struct cerco_resp_hdr *next;
  const char *name;
  const char *value;
} cerco_resp_hdr;

/* arena-backed response/request buffer */
typedef struct cerco_wbuf {
  cerco_arena *a;
  char *data;
  size_t len;
  size_t cap;
} cerco_wbuf;

/* write wrapper: tracks in-flight uv_write + malloc'd header/scratch buffer */
typedef struct {
  uv_write_t wr;
  cerco_req *req;   /* NULL for simple/sse writes */
  cerco_conn *conn;
  char *buf;        /* malloc'd buffer owned until callback */
} write_ctx;

void wbuf_init(cerco_wbuf *b, cerco_arena *a);
void wbuf_putn(cerco_wbuf *b, const void *data, size_t len);
void wbuf_puts(cerco_wbuf *b, const char *s);
/* bounded search over the (not NUL-terminated) buffer; NULL when absent */
char *wbuf_find(const cerco_wbuf *b, size_t from, const char *needle);
void wbuf_putc(cerco_wbuf *b, char c);
void wbuf_reset(cerco_wbuf *b);

typedef struct cerco_kv_list {
  const char *name;
  const char *value;
} cerco_kv_list;

typedef struct cerco_form_kv {
  struct cerco_form_kv *next;
  const char *name;
  const char *value;
} cerco_form_kv;

/* server-function context (public type, internal fields) */
struct cerco_sf_ctx {
  cerco_req *req;
  int failed;
  const char *error;
  /* wire reader over the request body (generated adapters pull args) */
  cerco_wreader reader;
  /* result storage filled by generated adapter */
  uint8_t ret_type;
  int32_t ret_i32;
  int64_t ret_i64;
  double ret_f64;
  uint8_t ret_bool;
  const char *ret_str;
  cerco_wbytes ret_bytes;
};

typedef enum {
  CERCO_PHASE_HEADERS = 0, /* waiting for full header block */
  CERCO_PHASE_BODY,        /* reading body */
  CERCO_PHASE_WORK,        /* request in worker */
  CERCO_PHASE_WRITE,       /* response being written */
  CERCO_PHASE_IDLE,        /* keep-alive idle */
  CERCO_PHASE_DRAIN,       /* half-closed: draining peer bytes before close */
} cerco_phase;

struct cerco_req {
  cerco_conn *conn;
  cerco_arena *arena;

  /* request data (all arena-owned) */
  char *method;
  char *target;         /* raw url as received */
  char *path;           /* decoded path */
  char *query;          /* raw query string, may be "" */
  cerco_kv_list headers[CERCO_MAX_HEADERS];
  int n_headers;
  char *body;
  size_t body_len;
  size_t body_cap;

  /* parsed lazily */
  cerco_form_kv *form;  /* parsed on demand from urlencoded body */
  char *route_params_value[16];
  const char *route_params_name[16];
  int n_route_params;

  /* response */
  int status;
  cerco_resp_hdr *resp_hdrs;
  cerco_resp_hdr *resp_hdrs_tail;
  int resp_hdrs_count;
  cerco_wbuf resp;      /* body buffer */
  const char *page_title; /* set by cerco_title(); patched in at finalize */
  int head_injected;    /* dev live-reload script */
  int responded;        /* response fully staged */

  /* lifecycle */
  atomic_int cancelled;
  int force_close;      /* send Connection: close */
  void (*handler)(cerco_req *r);  /* route handler (incl. layout chain) */
  void *sf_entry;       /* cerco_sf_entry* when this is a server-function call */
};

struct cerco_conn {
  cerco_server *srv;
  uv_tcp_t stream;
  llhttp_t parser;
  llhttp_settings_t *settings;

  cerco_arena arena;          /* request arena, reset between requests */
  cerco_req *req;             /* current request (arena-backed) */

  uint8_t *read_buf;
  size_t read_cap;
  size_t filled;
  size_t consumed;

  /* header accumulation (current pair, heap staging) */
  cerco_sb f_sb, v_sb;
  int in_value;
  cerco_sb url_sb;
  int bad_request;      /* set by parse errors / limits */
  int status_sent;      /* error response already sent */

  cerco_phase phase;
  int64_t deadline_ms;
  int keep_alive;
  int closing;
  int finalized;
  int close_cb_done;    /* uv_close callback ran: handle memory reusable */
  int force_close;      /* respond then drop the connection */
  int pending_writes;   /* queued uv_write requests */
  int msg_started, msg_completed;

  cerco_conn *prev, *next;   /* active list + pool */
  int sse;                   /* dev live-reload stream */
  struct cerco_conn *sse_next, *sse_prev;
};

typedef struct cerco_job {
  cerco_req *req;
} cerco_job;

typedef struct cerco_wpool {
  cerco_server *srv;
  uv_thread_t *threads;
  int n_threads;

  cerco_job *jobs;
  size_t job_cap, job_head, job_tail, job_count;
  uv_mutex_t job_mu;
  uv_cond_t job_cv;

  cerco_req **done;
  size_t done_cap, done_head, done_tail, done_count;
  uv_mutex_t done_mu;
  uv_async_t done_async;
  int done_async_inited;

  atomic_int shutdown;
  atomic_int jobs_inflight;
} cerco_wpool;

typedef struct cerco_stats {
  atomic_long active_conns;
  atomic_long total_conns;
  atomic_long active_reqs;
  atomic_long total_reqs;
  atomic_long queued_jobs;
  atomic_long completed_jobs;
  atomic_long rejected_jobs;
  atomic_long bytes_read;
  atomic_long bytes_written;
  atomic_long r2xx, r3xx, r4xx, r5xx;
  atomic_long arena_hwm;
  atomic_long event_loop_lag_ms;
  atomic_long sse_clients;
} cerco_stats;

typedef struct cerco_config {
  const char *host;
  int port;
  int workers;
  size_t work_queue;
  long max_connections;
  size_t max_body;
  int log_level;         /* 0=trace .. 4=error */
  int trust_proxy;
  int dev_mode;
  const char *dist_dir;  /* dev asset dir */
  int stats_enabled;
  uint64_t shutdown_timeout_ms;
  uint64_t header_timeout_ms;
  uint64_t body_timeout_ms;
  uint64_t keep_alive_timeout_ms;
  uint64_t request_timeout_ms;
  size_t request_arena_first;
  size_t request_arena_cap;
} cerco_config;

struct cerco_server {
  cerco_config cfg;
  const cerco_app *app;
  uv_loop_t loop;
  uv_tcp_t listener;
  uv_timer_t sweep_timer;
  uv_signal_t sig_int, sig_term;
  uv_async_t stop_async;
  int stop_async_inited;
  atomic_int shutting_down;

  cerco_wpool pool;
  cerco_stats stats;

  cerco_conn *conns;         /* active list */
  cerco_conn *sse_clients;   /* dev reload stream list */
  long n_conns;

  /* conn pool (conns embed their warm request arena) */
  cerco_conn *conn_free;
  int conn_free_n;
  uint8_t *buf_free;
  size_t buf_free_n;

  /* route table parsed at startup */
  struct cerco_rt_route *rt;
  size_t rt_n;

  int64_t sweep_last;
};

/* --- logging -------------------------------------------------------------- */
enum { CERCO_LOG_TRACE = 0, CERCO_LOG_DEBUG, CERCO_LOG_INFO, CERCO_LOG_WARN, CERCO_LOG_ERROR };
void cerco_log(int level, const char *fmt, ...);
void cerco_log_set_level(int level);

/* --- config ---------------------------------------------------------------- */
void cerco_config_from_env(cerco_config *cfg);

/* --- http core ------------------------------------------------------------- */
void cerco_http_init(void);
void cerco_connection_cb(uv_stream_t *server, int status);
void conn_alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf);
void conn_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf);
void conn_close_and_free(cerco_conn *c);
void conn_close_graceful(cerco_conn *c);
void conn_maybe_finalize(cerco_conn *c);
void conn_send_simple(cerco_conn *c, int code, const char *extra_hdr,
                      const char *body, size_t body_len, int force_close);
void conn_start_response(cerco_req *r);
void conn_request_dispatch(cerco_conn *c);
void conn_sweep_deadlines(cerco_server *srv);
void conn_send_sse_reload(cerco_server *srv);

/* --- worker pool ------------------------------------------------------------ */
void wpool_on_async(uv_async_t *a);      /* uv_async cb: drain completions */
void wpool_wake_event_loop(cerco_server *srv);
int wpool_start(cerco_server *srv, int n_threads, size_t queue_cap); /* 0 ok, -1 full */
int wpool_try_submit(cerco_server *srv, cerco_req *req);
void wpool_drain_completions(cerco_server *srv);
void wpool_shutdown(cerco_server *srv);

/* --- request internals ------------------------------------------------------ */
void req_setup(cerco_req *r, cerco_conn *c);
void req_reset(cerco_req *r);
void req_finish_headers(cerco_conn *c);   /* commit last header pair */
int req_target_path(cerco_req *r); /* decode target into r->path/query; 0 ok, else http code */

/* --- routing ----------------------------------------------------------------- */
int router_init(cerco_server *srv);       /* parse app->routes */
void router_dispatch(cerco_req *r);       /* find + call, or 404/405 */
void router_send_404(cerco_req *r);
int router_probe(cerco_req *r, cerco_route_fn *fn, int *allowed_mask);

/* --- static assets ------------------------------------------------------------ */
void static_try_serve(cerco_req *r);      /* embedded/dev assets, else 404 */
int static_is_asset_path(const char *path);

/* --- server functions ---------------------------------------------------------- */
int sf_is_sf_path(const char *path);
void sf_dispatch(cerco_req *r, const char *id_str);   /* event loop side */
void sf_run(cerco_req *r);                            /* worker side */
void sf_handle_reserved(cerco_req *r);    /* health/stats/live */
int sf_arg_of_type(cerco_sf_ctx *ctx, char want, cerco_wval *v);
void conn_sse_write(cerco_conn *c, const char *data, size_t len);
void conn_on_sse_written(uv_write_t *w, int status);

/* --- dispatch glue ---------------------------------------------------------------- */
void conn_request_dispatch(cerco_conn *c);
void dev_inject_live_script(cerco_req *r);
void apply_page_title(cerco_req *r);
void wbuf_printf(cerco_wbuf *b, const char *fmt, ...);

/* --- util ----------------------------------------------------------------------- */
int64_t cerco_now_ms(void);
const char *cerco_status_text(int code);
int cerco_cpu_count(void);
int cerco_parse_i64(const char *s, long long *out);

#endif
