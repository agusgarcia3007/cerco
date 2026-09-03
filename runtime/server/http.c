/* HTTP connection handling: libuv event loop + llhttp parsing.
 *
 * Rules:
 *  - Only the event loop thread touches uv handles/sockets.
 *  - Dynamic request work runs on bounded workers via wpool_try_submit.
 *  - Static assets and reserved endpoints are served inline (no worker).
 *  - Every phase sets a deadline checked by the 1s sweep timer.
 *
 * Connection lifetime:
 *  - conn_close_and_free() marks closing, cancels in-flight work and closes
 *    the socket. Actual teardown (conn_finalize) is deferred until no writer
 *    and no worker still references the conn:
 *      - pending_writes counts queued uv_write requests,
 *      - phase == WORK means a worker holds req (in the conn's arena).
 *  - The request arena is embedded in the conn and reset (not freed) between
 *    requests, so worker references stay valid until completion drain.
 *
 * Read-buffer discipline:
 *  - alloc_cb compacts unparsed bytes to the front and hands the tail.
 *  - on_read feeds [0..filled) to llhttp; consumed tracks executed bytes.
 *  - on_message_complete pauses the parser and stops reading; leftover bytes
 *    (pipelined request) are resumed after the response is written.
 */
#include "internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static llhttp_settings_t g_settings;

/* ------------------------------------------------------------------ pools */

static uint8_t *buf_get(cerco_server *srv) {
  if (srv->buf_free_n > 0) {
    srv->buf_free_n--;
    uint8_t *p = srv->buf_free;
    srv->buf_free = *(uint8_t **)(void *)p;
    return p;
  }
  return (uint8_t *)malloc(CERCO_READ_BUF);
}

static void buf_put(cerco_server *srv, uint8_t *p) {
  if (srv->buf_free_n >= 256) { free(p); return; }
  *(uint8_t **)(void *)p = srv->buf_free;
  srv->buf_free = p;
  srv->buf_free_n++;
}

static cerco_conn *conn_get(cerco_server *srv) {
  if (srv->conn_free) {
    cerco_conn *c = srv->conn_free;
    srv->conn_free = c->next;
    srv->conn_free_n--;
    return c;
  }
  return (cerco_conn *)calloc(1, sizeof(cerco_conn));
}

static void conn_put(cerco_server *srv, cerco_conn *c) {
  if (srv->conn_free_n >= 256) { free(c); return; }
  c->next = srv->conn_free;
  srv->conn_free = c;
  srv->conn_free_n++;
}

/* -------------------------------------------------------------- finalize */

static void conn_unlink(cerco_server *srv, cerco_conn *c) {
  if (c->prev) c->prev->next = c->next;
  else if (srv->conns == c) srv->conns = c->next;
  if (c->next) c->next->prev = c->prev;
  else if (srv->conns == c) srv->conns = NULL;
  c->prev = c->next = NULL;
}

/* idempotent teardown; must only run when no worker/write refs remain */
static void conn_finalize(cerco_server *srv, cerco_conn *c) {
  if (c->finalized) return;
  c->finalized = 1;
  conn_unlink(srv, c);
  srv->n_conns--;
  atomic_fetch_sub(&srv->stats.active_conns, 1);
  sb_free(&c->f_sb);
  sb_free(&c->v_sb);
  sb_free(&c->url_sb);
  if (c->read_buf) { buf_put(srv, c->read_buf); c->read_buf = NULL; }
  if (srv->conn_free_n < 256) {
    if (c->arena.head) cerco_arena_reset(&c->arena); /* keep warm chunk */
    conn_put(srv, c);
  } else {
    /* pool full: release everything, especially the arena chunk */
    if (c->arena.head) cerco_arena_destroy(&c->arena);
    free(c);
  }
}

static void conn_on_stream_closed(uv_handle_t *h) {
  cerco_conn *c = (cerco_conn *)h->data;
  if (!c) return;
  c->close_cb_done = 1;
  conn_maybe_finalize(c);
}

/* called after close or completion; finalizes when refs are gone.
 * IMPORTANT: the conn struct must outlive the queued uv_close (the uv_tcp_t
 * lives inside it), so finalization waits for close_cb_done. */
void conn_maybe_finalize(cerco_conn *c) {
  if (!c->closing || c->finalized) return;
  if (!c->close_cb_done) return;
  if (c->pending_writes > 0) return;
  if (c->req && c->phase == CERCO_PHASE_WORK) return;
  conn_finalize(c->srv, c);
}

void conn_close_and_free(cerco_conn *c) {
  cerco_server *srv = c->srv;
  if (c->closing) return;
  c->closing = 1;
  if (c->sse) {
    c->sse = 0;
    if (c->sse_prev) c->sse_prev->sse_next = c->sse_next; else srv->sse_clients = c->sse_next;
    if (c->sse_next) c->sse_next->sse_prev = c->sse_prev;
    atomic_fetch_sub(&srv->stats.sse_clients, 1);
  }
  if (c->req) atomic_store(&c->req->cancelled, 1);
  uv_read_stop((uv_stream_t *)&c->stream);
  uv_close((uv_handle_t *)&c->stream, conn_on_stream_closed);
  conn_maybe_finalize(c);
}

static void on_shutdown_done(uv_shutdown_t *shr, int status) {
  (void)status;
  free(shr);
}

/* Half-close: FIN the write side, keep draining the peer briefly so the
 * error response is delivered before any RST. Sweep closes stragglers. */
void conn_close_graceful(cerco_conn *c) {
  if (c->closing) return;
  c->phase = CERCO_PHASE_DRAIN;
  c->deadline_ms = cerco_now_ms() + 300;
  /* the response write is complete: release everything but the warm chunk */
  if (c->arena.head) cerco_arena_reset(&c->arena);
  uv_shutdown_t *shr = (uv_shutdown_t *)malloc(sizeof(uv_shutdown_t));
  if (shr) {
    int rc = uv_shutdown(shr, (uv_stream_t *)&c->stream, on_shutdown_done);
    if (rc) free(shr);
  }
  uv_read_start((uv_stream_t *)&c->stream, conn_alloc_cb, conn_on_read);
}

/* ------------------------------------------------------------------ writes */

static void after_response(cerco_req *r, int ok) {
  cerco_conn *c = r->conn;
  cerco_server *srv = c->srv;
  atomic_fetch_sub(&srv->stats.active_reqs, 1);
  if (!ok) { conn_close_and_free(c); conn_maybe_finalize(c); return; }
  if (c->closing) {
    c->req = NULL;
    conn_maybe_finalize(c);
    return;
  }
  if (c->force_close || !c->keep_alive) {
    conn_close_graceful(c);
    return;
  }
  c->phase = CERCO_PHASE_IDLE;
  c->deadline_ms = cerco_now_ms() + (int64_t)srv->cfg.keep_alive_timeout_ms;
  c->req = NULL;
  c->msg_started = 0;
  c->msg_completed = 0;
  c->status_sent = 0;
  c->bad_request = 0;
  c->force_close = 0;
  cerco_arena_reset(&c->arena);

  /* resume parsing for pipelined bytes */
  llhttp_resume(&c->parser);
  size_t leftover = c->filled - c->consumed;
  if (leftover > 0) memmove(c->read_buf, c->read_buf + c->consumed, leftover);
  c->filled = leftover;
  c->consumed = 0;
  if (leftover > 0) {
    llhttp_errno_t err = llhttp_execute(&c->parser, (const char *)c->read_buf, leftover);
    if (err == HPE_OK) {
      c->consumed = leftover;
    } else if (err == HPE_PAUSED) {
      c->consumed = (size_t)(llhttp_get_error_pos(&c->parser) - (const char *)c->read_buf);
    } else {
      c->force_close = 1;
      conn_send_simple(c, 400, NULL, "bad request\n", 12, 1);
      return;
    }
  }
  uv_read_start((uv_stream_t *)&c->stream, conn_alloc_cb, conn_on_read);
}

static void on_response_written(uv_write_t *w, int status) {
  write_ctx *ctx = (write_ctx *)w;
  cerco_conn *c = ctx->conn;
  cerco_req *r = ctx->req;
  free(ctx->buf);
  free(ctx);
  c->pending_writes--;
  if (r && !c->finalized) after_response(r, status == 0);
  else conn_maybe_finalize(c);
}

void conn_start_response(cerco_req *r) {
  cerco_conn *c = r->conn;
  cerco_server *srv = c->srv;
  c->phase = CERCO_PHASE_WRITE;
  c->deadline_ms = cerco_now_ms() + (int64_t)srv->cfg.request_timeout_ms;

  int code = r->status;
  int is_head = llhttp_get_method(&c->parser) == HTTP_HEAD;
  int no_body = is_head || code == 204 || code == 304;

  /* status counters */
  if (code < 300) atomic_fetch_add(&srv->stats.r2xx, 1);
  else if (code < 400) atomic_fetch_add(&srv->stats.r3xx, 1);
  else if (code < 500) atomic_fetch_add(&srv->stats.r4xx, 1);
  else atomic_fetch_add(&srv->stats.r5xx, 1);

  cerco_sb hdr;
  sb_init(&hdr);
  sb_printf(&hdr, "HTTP/1.1 %d %s\r\n", code, cerco_status_text(code));
  int have_ct = 0, have_cl = 0, have_connection = 0;
  for (cerco_resp_hdr *h = r->resp_hdrs; h; h = h->next) {
    if (cerco_strcaseeq(h->name, "content-type")) have_ct = 1;
    if (cerco_strcaseeq(h->name, "content-length")) have_cl = 1;
    if (cerco_strcaseeq(h->name, "connection")) have_connection = 1;
    sb_printf(&hdr, "%s: %s\r\n", h->name, h->value);
  }
  if (!have_ct && !no_body) sb_puts(&hdr, "Content-Type: text/html; charset=utf-8\r\n");
  if (!have_cl && code != 304) sb_printf(&hdr, "Content-Length: %zu\r\n", r->resp.len);
  if (!have_connection)
    sb_puts(&hdr, (c->force_close || !c->keep_alive) ? "Connection: close\r\n"
                                                     : "Connection: keep-alive\r\n");
  sb_puts(&hdr, "X-Content-Type-Options: nosniff\r\n");
  sb_puts(&hdr, "Referrer-Policy: strict-origin-when-cross-origin\r\n");
  sb_puts(&hdr, "Server: cerco\r\n\r\n");

  write_ctx *ctx = (write_ctx *)malloc(sizeof(write_ctx));
  if (!ctx) { sb_free(&hdr); conn_close_and_free(c); return; }
  ctx->req = r;
  ctx->conn = c;
  ctx->buf = hdr.buf; /* freed after write */

  uv_buf_t bufs[2];
  bufs[0] = uv_buf_init(hdr.buf, (unsigned int)hdr.len);
  bufs[1] = no_body || r->resp.len == 0 ? uv_buf_init(NULL, 0)
                                        : uv_buf_init(r->resp.data, (unsigned int)r->resp.len);
  atomic_fetch_add(&srv->stats.bytes_written, (long)hdr.len);
  c->pending_writes++;
  int rc = uv_write(&ctx->wr, (uv_stream_t *)&c->stream, bufs, 2, on_response_written);
  if (rc) {
    c->pending_writes--;
    free(ctx->buf);
    free(ctx);
    conn_close_and_free(c);
  }
}

static void on_simple_written(uv_write_t *w, int status);

void conn_send_simple(cerco_conn *c, int code, const char *extra_hdr,
                      const char *body, size_t body_len, int force_close) {
  cerco_server *srv = c->srv;
  const char *ctext = cerco_status_text(code);
  if (!ctext[0]) ctext = "Error";

  size_t cap = 256 + (extra_hdr ? strlen(extra_hdr) : 0) + body_len + 64;
  char *buf = (char *)malloc(cap);
  if (!buf) { conn_close_and_free(c); return; }
  int n = snprintf(buf, cap,
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: text/plain; charset=utf-8\r\n"
                   "Content-Length: %zu\r\n"
                   "%s%s"
                   "Connection: %s\r\n"
                   "X-Content-Type-Options: nosniff\r\n"
                   "Server: cerco\r\n\r\n",
                   code, ctext, body_len, extra_hdr ? extra_hdr : "",
                   extra_hdr ? "\r\n" : "",
                   force_close ? "close" : "keep-alive");
  if (body_len && n > 0) memcpy(buf + n, body, body_len);
  size_t out_len = (size_t)n + body_len;

  write_ctx *ctx = (write_ctx *)malloc(sizeof(write_ctx));
  if (!ctx) { free(buf); conn_close_and_free(c); return; }
  ctx->req = NULL;
  ctx->conn = c;
  ctx->buf = buf;

  uv_buf_t b = uv_buf_init(buf, (unsigned int)out_len);
  atomic_fetch_add(&srv->stats.bytes_written, (long)out_len);
  if (code >= 400) atomic_fetch_add(&srv->stats.r4xx, 1);
  c->keep_alive = !force_close;
  c->force_close = force_close;
  c->status_sent = 1;
  c->pending_writes++;
  int rc = uv_write(&ctx->wr, (uv_stream_t *)&c->stream, &b, 1, on_simple_written);
  if (rc) {
    c->pending_writes--;
    free(ctx->buf);
    free(ctx);
    conn_close_and_free(c);
  }
}

static void on_simple_written(uv_write_t *w, int status) {
  write_ctx *ctx = (write_ctx *)w;
  cerco_conn *c = ctx->conn;
  free(ctx->buf);
  free(ctx);
  c->pending_writes--;
  atomic_fetch_sub(&c->srv->stats.active_reqs, 1);
  if (c->finalized || c->closing) { conn_maybe_finalize(c); return; }
  if (status) { conn_close_and_free(c); return; }
  if (c->keep_alive && !c->force_close) {
    /* recoverable error response on a live conn (e.g. 503 queue full) */
    c->phase = CERCO_PHASE_IDLE;
    c->deadline_ms = cerco_now_ms() + (int64_t)c->srv->cfg.keep_alive_timeout_ms;
    c->status_sent = 0;
    c->bad_request = 0;
    c->filled = 0;
    c->consumed = 0;
    llhttp_resume(&c->parser);
    uv_read_start((uv_stream_t *)&c->stream, conn_alloc_cb, conn_on_read);
  } else {
    conn_close_graceful(c);
  }
}

/* ------------------------------------------------------------------ SSE dev */

void conn_send_sse_reload(cerco_server *srv) {
  for (cerco_conn *c = srv->sse_clients; c; c = c->sse_next) {
    if (!c->closing) conn_sse_write(c, "data: reload\n\n", 14);
  }
}

/* ------------------------------------------------------------------ llhttp */

static int on_message_begin(llhttp_t *p) {
  cerco_conn *c = (cerco_conn *)p->data;
  cerco_server *srv = c->srv;
  if (c->req) {
    /* second message began while first still open: abort */
    c->force_close = 1;
    return 0;
  }
  if (!c->arena.head) {
    cerco_arena_init(&c->arena, srv->cfg.request_arena_first, srv->cfg.request_arena_cap);
  }
  c->req = (cerco_req *)cerco_arena_alloc(&c->arena, sizeof(cerco_req));
  if (!c->req) return -1;
  req_setup(c->req, c);
  c->msg_started++;
  c->phase = CERCO_PHASE_HEADERS;
  c->deadline_ms = cerco_now_ms() + (int64_t)srv->cfg.header_timeout_ms;
  return 0;
}

static int on_url(llhttp_t *p, const char *at, size_t len) {
  cerco_conn *c = (cerco_conn *)p->data;
  if (c->url_sb.len + len > CERCO_MAX_URL) { c->bad_request = 414; return -1; }
  sb_putn(&c->url_sb, at, len);
  return 0;
}

static int on_header_field(llhttp_t *p, const char *at, size_t len) {
  cerco_conn *c = (cerco_conn *)p->data;
  if (c->bad_request) return -1;
  if (c->f_sb.len || c->v_sb.len) req_finish_headers(c); /* commit previous pair */
  if (c->bad_request) return -1;
  if (c->f_sb.len + len > CERCO_MAX_NAME) { c->bad_request = 431; return -1; }
  sb_putn(&c->f_sb, at, len);
  return 0;
}

static int on_header_value(llhttp_t *p, const char *at, size_t len) {
  cerco_conn *c = (cerco_conn *)p->data;
  if (c->bad_request) return -1;
  c->in_value = 1;
  if (c->v_sb.len + len > CERCO_MAX_VALUE) { c->bad_request = 431; return -1; }
  sb_putn(&c->v_sb, at, len);
  return 0;
}

static int on_headers_complete(llhttp_t *p) {
  cerco_conn *c = (cerco_conn *)p->data;
  if (c->bad_request) return -1;
  req_finish_headers(c);
  if (c->bad_request) return -1;
  cerco_server *srv = c->srv;
  cerco_req *r = c->req;
  if (!r) return -1;
  if (p->content_length > 0 && (uint64_t)p->content_length > srv->cfg.max_body) {
    c->bad_request = 413;
    return -1;
  }
  if (p->content_length > 0) {
    r->body = (char *)cerco_arena_alloc(r->arena, (size_t)p->content_length);
    r->body_cap = (size_t)p->content_length;
    if (!r->body) { c->bad_request = 413; return -1; }
  } else if (p->flags & F_CHUNKED) {
    r->body = (char *)cerco_arena_alloc(r->arena, 4096);
    r->body_cap = 4096;
    if (!r->body) { c->bad_request = 413; return -1; }
  }
  c->phase = CERCO_PHASE_BODY;
  c->deadline_ms = cerco_now_ms() + (int64_t)srv->cfg.body_timeout_ms;
  return 0;
}

static int on_body(llhttp_t *p, const char *at, size_t len) {
  cerco_conn *c = (cerco_conn *)p->data;
  cerco_req *r = c->req;
  if (!r) return 0;
  if (r->body_len + len > c->srv->cfg.max_body) { c->bad_request = 413; return -1; }
  if (r->body_len + len > r->body_cap) {
    size_t ncap = r->body_cap ? r->body_cap * 2 : 4096;
    while (ncap < r->body_len + len) ncap *= 2;
    char *nb = (char *)cerco_arena_alloc(r->arena, ncap);
    if (!nb) { c->bad_request = 413; return -1; }
    memcpy(nb, r->body, r->body_len);
    r->body = nb;
    r->body_cap = ncap;
  }
  memcpy(r->body + r->body_len, at, len);
  r->body_len += len;
  return 0;
}

static int on_message_complete(llhttp_t *p) {
  cerco_conn *c = (cerco_conn *)p->data;
  c->msg_completed++;
  c->keep_alive = llhttp_should_keep_alive(p) ? 1 : 0;
  if (c->req) {
    llhttp_method_t m = llhttp_get_method(p);
    const char *mn = llhttp_method_name(m);
    c->req->method = cerco_arena_strdup(c->req->arena, mn ? mn : "");
    c->req->target = c->url_sb.len
        ? cerco_arena_strndup(c->req->arena, c->url_sb.buf, c->url_sb.len)
        : (char *)"/";
    c->url_sb.len = 0;
    if (c->url_sb.buf) c->url_sb.buf[0] = 0;
    int tgt_err = req_target_path(c->req);
    if (tgt_err) {
      c->bad_request = tgt_err;
      return -1;
    }
  }
  atomic_fetch_add(&c->srv->stats.active_reqs, 1);
  atomic_fetch_add(&c->srv->stats.total_reqs, 1);
  conn_request_dispatch(c);
  llhttp_pause(p); /* hold remaining (pipelined) bytes until response written */
  return 0;
}

void cerco_http_init(void) {
  llhttp_settings_init(&g_settings);
  g_settings.on_message_begin = on_message_begin;
  g_settings.on_url = on_url;
  g_settings.on_header_field = on_header_field;
  g_settings.on_header_value = on_header_value;
  g_settings.on_headers_complete = on_headers_complete;
  g_settings.on_body = on_body;
  g_settings.on_message_complete = on_message_complete;
}

/* ------------------------------------------------------------------ read */

void conn_alloc_cb(uv_handle_t *handle, size_t suggested, uv_buf_t *buf) {
  (void)suggested;
  cerco_conn *c = (cerco_conn *)handle->data;
  if (!c || c->closing || c->finalized) {
    buf->base = NULL;
    buf->len = 0;
    return;
  }
  if (!c->read_buf) {
    c->read_buf = buf_get(c->srv);
    if (!c->read_buf) {
      buf->base = NULL;
      buf->len = 0;
      return;
    }
    c->filled = 0;
    c->consumed = 0;
  } else {
    /* compaction: move un-executed bytes to the front (parser paused) */
    size_t leftover = c->filled - c->consumed;
    if (leftover && c->consumed) {
      memmove(c->read_buf, c->read_buf + c->consumed, leftover);
    }
    c->filled = leftover;
    c->consumed = 0;
  }
  /* never hand libuv a zero-length buffer: it reports UV_ENOBUFS */
  buf->base = (char *)c->read_buf + c->filled;
  buf->len = CERCO_READ_BUF - c->filled;
}

void conn_on_read(uv_stream_t *stream, ssize_t nread, const uv_buf_t *buf) {
  (void)buf;
  cerco_conn *c = (cerco_conn *)stream->data;
  if (!c) return;
  cerco_server *srv = c->srv;
  if (nread < 0) {
    conn_close_and_free(c);
    return;
  }
  if (nread == 0) return;
  atomic_fetch_add(&srv->stats.bytes_read, (long)nread);
  c->filled += (size_t)nread;

  if (c->status_sent || c->phase == CERCO_PHASE_DRAIN) {
    /* draining bytes of a connection we already answered with an error */
    if (c->filled >= CERCO_READ_BUF) conn_close_and_free(c);
    return;
  }

  llhttp_errno_t err = llhttp_execute(&c->parser, (const char *)c->read_buf, c->filled);
  if (err == HPE_OK) {
    c->consumed = c->filled;
  } else if (err == HPE_PAUSED) {
    c->consumed = (size_t)(llhttp_get_error_pos(&c->parser) - (const char *)c->read_buf);
    uv_read_stop(stream); /* response pending; resume after write */
  } else {
    c->consumed = (size_t)(llhttp_get_error_pos(&c->parser) - (const char *)c->read_buf);
    int code = 400;
    const char *reason = "malformed request\n";
    if (c->bad_request) {
      code = c->bad_request;
      if (code == 431) reason = "header fields too large\n";
      else if (code == 413) reason = "payload too large\n";
      else if (code == 414) reason = "uri too long\n";
    } else if (err == HPE_INVALID_METHOD) reason = "invalid method\n";
    else if (err == HPE_INVALID_URL) reason = "invalid url\n";
    c->force_close = 1;
    conn_send_simple(c, code, NULL, reason, strlen(reason), 1);
  }
}

/* ------------------------------------------------------------------ accept */

void cerco_connection_cb(uv_stream_t *server, int status) {
  cerco_server *srv = (cerco_server *)server->data;
  if (status || atomic_load(&srv->shutting_down)) return;

  cerco_conn *c = conn_get(srv);
  if (!c) return;
  memset(c, 0, sizeof(*c));
  c->srv = srv;
  c->keep_alive = 1;

  if (uv_tcp_init(&srv->loop, &c->stream) != 0) {
    conn_put(srv, c);
    return;
  }
  c->stream.data = c;
  if (uv_accept(server, (uv_stream_t *)&c->stream) != 0) {
    uv_close((uv_handle_t *)&c->stream, conn_on_stream_closed);
    conn_maybe_finalize(c);
    return;
  }

  /* link + count before overflow check so teardown stays consistent */
  c->next = srv->conns;
  c->prev = NULL;
  if (srv->conns) srv->conns->prev = c;
  srv->conns = c;
  srv->n_conns++;
  atomic_fetch_add(&srv->stats.active_conns, 1);
  atomic_fetch_add(&srv->stats.total_conns, 1);

  llhttp_init(&c->parser, HTTP_REQUEST, &g_settings);
  c->parser.data = c;
  sb_init(&c->f_sb);
  sb_init(&c->v_sb);
  sb_init(&c->url_sb);
  c->phase = CERCO_PHASE_HEADERS;
  c->deadline_ms = cerco_now_ms() + (int64_t)srv->cfg.header_timeout_ms;

  uv_tcp_nodelay(&c->stream, 1);

  if (srv->n_conns > srv->cfg.max_connections) {
    conn_send_simple(c, 503, NULL, "server busy\n", 12, 1);
    return;
  }

  uv_read_start((uv_stream_t *)&c->stream, conn_alloc_cb, conn_on_read);
}

/* ------------------------------------------------------------------ sweep */

static void conn_deadline_pass(cerco_conn *c) {
  if (c->req) atomic_store(&c->req->cancelled, 1);
  c->force_close = 1;
  conn_close_and_free(c);
}

void conn_sweep_deadlines(cerco_server *srv) {
  int64_t now = cerco_now_ms();
  cerco_conn *c = srv->conns;
  while (c) {
    cerco_conn *next = c->next;
    if (!c->closing && !c->finalized && now >= c->deadline_ms) conn_deadline_pass(c);
    c = next;
  }
}
