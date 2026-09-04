/* DOM command buffer + signals + bindings.
 *
 * Commands are appended to a flat byte buffer and flushed to the host in one
 * import call per batch (minimizes wasm<->js crossings). Strings are stored
 * inline as [u32 len][bytes]. All integers LE via memcpy on a packed struct
 * write helper.
 */
#include "cerco_client.h"
#include "client_internal.h"
#include "wasm_libc.h"

/* -------------------------------------------------------- command buffer */

#define CMD_BUF_SIZE 32768

static uint8_t *cmd_buf;
static size_t cmd_len;

enum {
  OP_CREATE = 1,     /* u32 new_id, str tag, u32 parent */
  OP_SET_TEXT,       /* u32 node, str text */
  OP_SET_ATTR,       /* u32 node, str name, str value */
  OP_REMOVE_ATTR,    /* u32 node, str name */
  OP_APPEND_CHILD,   /* u32 parent, u32 child */
  OP_REMOVE_NODE,    /* u32 node */
  OP_SET_CLASS,      /* u32 node, str class */
  OP_ADD_CLASS,      /* u32 node, str class */
  OP_REMOVE_CLASS,   /* u32 node, str class */
  OP_ADD_EVENT,      /* u32 node, str type, u32 slot */
  OP_SET_VALUE,      /* u32 node, str value */
  OP_SET_INNER_HTML, /* u32 node, str html */
};

static void cmd_put_u8(uint8_t v) { cmd_buf[cmd_len++] = v; }

static void cmd_put_u32(uint32_t v) {
  cmd_buf[cmd_len++] = (uint8_t)v;
  cmd_buf[cmd_len++] = (uint8_t)(v >> 8);
  cmd_buf[cmd_len++] = (uint8_t)(v >> 16);
  cmd_buf[cmd_len++] = (uint8_t)(v >> 24);
}

static void cmd_put_str(const char *s) {
  uint32_t n = s ? (uint32_t)strlen(s) : 0;
  cmd_put_u32(n);
  if (n) {
    memcpy(cmd_buf + cmd_len, s, n);
    cmd_len += n;
  }
}

static void cmd_begin(uint8_t op) {
  if (!cmd_buf) cmd_buf = cerco_alloc(CMD_BUF_SIZE);
  if (!cmd_buf) return;
  cmd_put_u8(op);
}

static void cmd_end(void) {
  /* flush eagerly when buffer is 3/4 full to bound latency+memory */
  if (cmd_len > CMD_BUF_SIZE - CMD_BUF_SIZE / 4) {
    host_dom_flush((int32_t)cmd_buf, (int32_t)cmd_len);
    cmd_len = 0;
  }
}

void cerco_dom_flush(void) {
  if (cmd_len) {
    host_dom_flush((int32_t)cmd_buf, (int32_t)cmd_len);
    cmd_len = 0;
  }
}

/* ------------------------------------------------------------------ dom ops */

void cerco_set_text(int32_t node, const char *s) {
  cmd_begin(OP_SET_TEXT);
  cmd_put_u32((uint32_t)node);
  cmd_put_str(s);
  cmd_end();
}

void cerco_set_attr(int32_t node, const char *k, const char *v) {
  cmd_begin(OP_SET_ATTR);
  cmd_put_u32((uint32_t)node);
  cmd_put_str(k);
  cmd_put_str(v);
  cmd_end();
}

void cerco_remove_attr(int32_t node, const char *k) {
  cmd_begin(OP_REMOVE_ATTR);
  cmd_put_u32((uint32_t)node);
  cmd_put_str(k);
  cmd_end();
}

void cerco_set_class(int32_t node, const char *cls) {
  cmd_begin(OP_SET_CLASS);
  cmd_put_u32((uint32_t)node);
  cmd_put_str(cls);
  cmd_end();
}

void cerco_add_class(int32_t node, const char *cls) {
  cmd_begin(OP_ADD_CLASS);
  cmd_put_u32((uint32_t)node);
  cmd_put_str(cls);
  cmd_end();
}

void cerco_remove_class(int32_t node, const char *cls) {
  cmd_begin(OP_REMOVE_CLASS);
  cmd_put_u32((uint32_t)node);
  cmd_put_str(cls);
  cmd_end();
}

int32_t cerco_create(const char *tag, int32_t parent) {
  static uint32_t next_dynamic_id = 1u << 20;
  uint32_t id = next_dynamic_id++;
  cmd_begin(OP_CREATE);
  cmd_put_u32(id);
  cmd_put_str(tag);
  cmd_put_u32((uint32_t)parent);
  cmd_end();
  return (int32_t)id;
}

void cerco_remove(int32_t node) {
  cmd_begin(OP_REMOVE_NODE);
  cmd_put_u32((uint32_t)node);
  cmd_end();
}

void cerco_set_value(int32_t node, const char *v) {
  cmd_begin(OP_SET_VALUE);
  cmd_put_u32((uint32_t)node);
  cmd_put_str(v);
  cmd_end();
}

void cerco_set_inner_html(int32_t node, const char *html) {
  cmd_begin(OP_SET_INNER_HTML);
  cmd_put_u32((uint32_t)node);
  cmd_put_str(html);
  cmd_end();
}

int32_t cerco_query(int32_t scope, const char *selector) {
  char *sel = cerco_scratch_strdup(selector);
  if (!sel) return 0;
  return host_query(scope, (int32_t)sel, (int32_t)strlen(sel));
}

int32_t cerco_value(int32_t node, char *buf, int32_t cap) {
  return host_value(node, (int32_t)buf, cap);
}

int32_t cerco_attr(int32_t node, const char *name, char *buf, int32_t cap) {
  if (!node || !name || !buf || cap <= 0) return -1;
  cerco_dom_flush(); /* pending set_attr commands must land before we read */
  int32_t n = host_attr(node, (int32_t)name, (int32_t)strlen(name),
                        (int32_t)buf, cap - 1);
  buf[n > 0 ? n : 0] = 0; /* always a valid C string, empty when absent */
  return n;
}

/* ---------------------------------------------------------------- signals */

typedef struct cerco_sub {
  void (*fn)(struct cerco_sig *s, void *ctx);
  void *ctx;
  struct cerco_sub *next;
} cerco_sub;

struct cerco_sig {
  int32_t value;
  cerco_sub *subs;
};

cerco_sig *cerco_signal_new(int32_t initial) {
  cerco_sig *s = cerco_alloc(sizeof(cerco_sig));
  if (!s) return 0;
  s->value = initial;
  s->subs = 0;
  return s;
}

int32_t cerco_signal_value(cerco_sig *s) { return s ? s->value : 0; }

int32_t cerco_signal_get(cerco_sig *s) { return s ? s->value : 0; }

void cerco_signal_set(cerco_sig *s, int32_t v) {
  if (!s || s->value == v) return;
  s->value = v;
  for (cerco_sub *sub = s->subs; sub; sub = sub->next) {
    sub->fn(s, sub->ctx);
  }
  cerco_dom_flush();
}

/* ------------------------------------------------------------- bindings */

static void bind_text_run(cerco_sig *s, void *ctx) {
  (void)s;
  int32_t node = (int32_t)(uintptr_t)ctx;
  char buf[16];
  cerco_i32_to_str(buf, s->value);
  cerco_set_text(node, buf);
}

void cerco_bind_text(int32_t node, cerco_sig *s) {
  if (!s) return;
  cerco_sub *sub = cerco_alloc(sizeof(cerco_sub));
  if (!sub) return;
  sub->fn = bind_text_run;
  sub->ctx = (void *)(uintptr_t)node;
  sub->next = s->subs;
  s->subs = sub;
  /* sync current value */
  char buf[16];
  cerco_i32_to_str(buf, s->value);
  cerco_set_text(node, buf);
  cerco_dom_flush();
}

typedef struct {
  int32_t node;
  const char *cls;
} class_binding;

static void bind_class_run(cerco_sig *s, void *ctx) {
  class_binding *b = ctx;
  if (s->value) cerco_add_class(b->node, b->cls);
  else cerco_remove_class(b->node, b->cls);
}

void cerco_bind_class(int32_t node, cerco_sig *s, const char *cls) {
  if (!s) return;
  class_binding *b = cerco_alloc(sizeof(class_binding));
  if (!b) return;
  b->node = node;
  b->cls = cerco_strdup(cls);
  cerco_sub *sub = cerco_alloc(sizeof(cerco_sub));
  if (!sub) return;
  sub->fn = bind_class_run;
  sub->ctx = b;
  sub->next = s->subs;
  s->subs = sub;
  if (s->value) cerco_add_class(node, cls);
  else cerco_remove_class(node, cls);
  cerco_dom_flush();
}

/* ---------------------------------------------------------------- events */

#define MAX_EVENT_SLOTS 512

typedef struct {
  cerco_ev_fn fn;
  void *user;
} event_slot;

static event_slot g_slots[MAX_EVENT_SLOTS];
static uint32_t g_slot_count;

void cerco_on(int32_t node, const char *event, cerco_ev_fn fn, void *user) {
  if (g_slot_count >= MAX_EVENT_SLOTS) return;
  uint32_t slot = g_slot_count++;
  g_slots[slot].fn = fn;
  g_slots[slot].user = user;
  cmd_begin(OP_ADD_EVENT);
  cmd_put_u32((uint32_t)node);
  cmd_put_str(event);
  cmd_put_u32(slot);
  cmd_end();
}

__attribute__((export_name("cerco_event")))
void cerco_event_dispatch(int32_t node, int32_t slot) {
  if (slot < 0 || (uint32_t)slot >= g_slot_count) return;
  cerco_scratch_reset();
  g_slots[slot].fn(node, g_slots[slot].user);
  cerco_dom_flush();
}

void cerco_events_reset(void) { g_slot_count = 0; }

/* ------------------------------------------------------------------- http */

#define MAX_PENDING_FETCHES 32
#define FETCH_SLOTS 4 /* concurrent response buffers */
/* Hard cap for one response body. Buffers are allocated to the size the
 * response actually needs (the host reports it before writing), so small
 * requests cost nothing and a large one is not silently cut in half.
 * Override with -DCERCO_FETCH_MAX at build time. */
#ifndef CERCO_FETCH_MAX
#define CERCO_FETCH_MAX (512 * 1024)
#endif

typedef struct {
  int used;
  cerco_http_cb cb;
  void *user;
  uint8_t *resp;   /* host writes the body here */
} pending_fetch;

static pending_fetch g_fetches[MAX_PENDING_FETCHES];
static uint8_t *g_fetch_bufs[FETCH_SLOTS];
static int32_t g_fetch_caps[FETCH_SLOTS];

static pending_fetch *fetch_reserve(void) {
  for (int i = 0; i < MAX_PENDING_FETCHES; i++) {
    if (!g_fetches[i].used) {
      g_fetches[i].used = 1;
      return &g_fetches[i];
    }
  }
  return 0; /* bounded: excess requests are rejected, never queued */
}

void cerco_http_post(const char *url, const uint8_t *body, int32_t len,
                     cerco_http_cb cb, void *user) {
  pending_fetch *f = fetch_reserve();
  if (!f) return;
  f->cb = cb;
  f->user = user;
  f->resp = 0; /* the host asks for a buffer once it knows the body size */
  char *u = cerco_scratch_strdup(url);
  uint8_t *bbuf = 0;
  if (body && len > 0) {
    bbuf = cerco_scratch_alloc((size_t)len);
    if (bbuf) memcpy(bbuf, body, (size_t)len);
  }
  host_fetch((int32_t)(f - g_fetches), (int32_t)"POST", 4, (int32_t)u,
             (int32_t)strlen(url), (int32_t)bbuf, bbuf ? len : 0);
}

void cerco_http_get(const char *url, cerco_http_cb cb, void *user) {
  pending_fetch *f = fetch_reserve();
  if (!f) return;
  f->cb = cb;
  f->user = user;
  f->resp = 0; /* the host asks for a buffer once it knows the body size */
  char *u = cerco_scratch_strdup(url);
  host_fetch((int32_t)(f - g_fetches), (int32_t)"GET", 3, (int32_t)u,
             (int32_t)strlen(url), 0, 0);
}

/* The host calls this once it knows the body size and before writing it:
 * hand back a buffer of exactly that size (plus a NUL, so callbacks can
 * treat a text body as a C string). Returns 0 when the body is over
 * CERCO_FETCH_MAX or the heap is exhausted, and the host then reports the
 * request as CERCO_HTTP_TOO_LARGE rather than delivering a truncated body. */
__attribute__((export_name("cerco_fetch_reserve")))
int32_t cerco_fetch_reserve(int32_t id, int32_t len) {
  if (id < 0 || id >= MAX_PENDING_FETCHES || !g_fetches[id].used) return 0;
  if (len < 0 || len > CERCO_FETCH_MAX) return 0;
  int slot = id % FETCH_SLOTS;
  int32_t need = len + 1;
  if (g_fetch_caps[slot] < need) {
    /* the client heap is a bump allocator: the previous buffer is abandoned
     * and reclaimed wholesale when navigation rewinds the heap */
    uint8_t *buf = cerco_alloc_sticky((size_t)need);
    if (!buf) return 0;
    g_fetch_bufs[slot] = buf;
    g_fetch_caps[slot] = need;
  }
  g_fetches[id].resp = g_fetch_bufs[slot];
  g_fetch_bufs[slot][len] = 0;
  return (int32_t)g_fetch_bufs[slot];
}

/* host calls into here when a fetch finishes; body is in f->resp */
__attribute__((export_name("cerco_fetch_done")))
void cerco_fetch_done(int32_t id, int32_t status, int32_t len) {
  if (id < 0 || id >= MAX_PENDING_FETCHES || !g_fetches[id].used) return;
  cerco_http_cb cb = g_fetches[id].cb;
  void *user = g_fetches[id].user;
  const uint8_t *data = g_fetches[id].resp;
  g_fetches[id].used = 0;
  g_fetches[id].resp = 0;
  cerco_scratch_reset();
  if (cb) cb(status, (len > 0 && data) ? data : 0, len, user);
  cerco_dom_flush();
}

/* ---------------------------------------------------------- hydration roots */

/* root ids are assigned by the host and looked up through an import */
__attribute__((import_module("cerco"), import_name("root_id")))
extern int32_t host_root_id(int32_t index);

int32_t cerco_root_node(int32_t index) {
  if (index < 0 || index >= 64) return 0;
  return host_root_id(index);
}

void cerco_roots_reset(void) {
  /* host side resets its root registry on re-scan */
}

/* ---------------------------------------------------------- json helpers */

int32_t cerco_json_int(const char *json, const char *key, int32_t dflt) {
  if (!json || !key) return dflt;
  char pat[128];
  size_t kl = strlen(key);
  if (kl + 4 >= sizeof(pat)) return dflt;
  pat[0] = '"';
  memcpy(pat + 1, key, kl);
  pat[kl + 1] = '"';
  pat[kl + 2] = ':';
  pat[kl + 3] = 0;
  const char *p = strstr(json, pat);
  if (!p) return dflt;
  p += kl + 3;
  while (*p == ' ') p++;
  int neg = 0;
  if (*p == '-') { neg = 1; p++; }
  if (*p < '0' || *p > '9') return dflt;
  int32_t v = 0;
  while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
  return neg ? -v : v;
}

int cerco_json_str(const char *json, const char *key, char *buf, int32_t cap) {
  if (!json || !key || !buf || cap < 1) return 0;
  char pat[128];
  size_t kl = strlen(key);
  if (kl + 4 >= sizeof(pat)) return 0;
  pat[0] = '"';
  memcpy(pat + 1, key, kl);
  pat[kl + 1] = '"';
  pat[kl + 2] = ':';
  pat[kl + 3] = 0;
  const char *p = strstr(json, pat);
  if (!p) return 0;
  p += kl + 3;
  while (*p == ' ') p++;
  if (*p != '"') return 0;
  p++;
  int32_t o = 0;
  while (*p && *p != '"') {
    if (o + 1 >= cap) return 0;
    buf[o++] = *p++;
  }
  if (*p != '"') return 0;
  buf[o] = 0;
  return 1;
}
