/* Reserved endpoints + server-function dispatch.
 *
 * SF protocol (POST by default):
 *   URL:  /__cerco/sf/<id>
 *   Body: [u32 arg_count][values...]          (LE integers)
 *   Resp: [u8 status][u32 err_len][err bytes][u32 value_count][values...]
 * Content-Type must be application/x-cerco-sf. Only registered functions
 * (generated table) are callable.
 *
 * Threading: sf_dispatch runs on the event loop (validation + job submit).
 * sf_run runs on a worker and ONLY stages a response; the completion drain
 * (event loop) performs all socket writes.
 */
#include "internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char SF_CONTENT_TYPE[] = "application/x-cerco-sf";

int sf_is_sf_path(const char *path) {
  if (!cerco_strncaseeq(path, "/__cerco/sf/", 12)) return 0;
  return path[12] != 0;
}

/* extract one arg of given type from the body reader (used by generated code) */
int sf_arg_of_type(cerco_sf_ctx *ctx, char want, cerco_wval *v) {
  if (!cerco_r_val(&ctx->reader, v)) return 0;
  char got = v->type == CERCO_WT_I32 ? 'i' : v->type == CERCO_WT_I64 ? 'l'
           : v->type == CERCO_WT_F64 ? 'f' : v->type == CERCO_WT_BOOL ? 'b'
           : v->type == CERCO_WT_STR ? 's' : v->type == CERCO_WT_BYTES ? 'y' : '?';
  if (want == got) return 1;
  /* int widening: bool <-> i32 */
  if ((want == 'i' && got == 'b') || (want == 'b' && got == 'i')) return 1;
  return 0;
}

static void sb_put_u32le(cerco_sb *sb, uint32_t v) {
  uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
  sb_putn(sb, b, 4);
}

/* event loop side: validate + submit */
void sf_dispatch(cerco_req *r, const char *id_str) {
  cerco_server *srv = r->conn->srv;
  uint32_t id = 0;
  {
    const char *p = id_str;
    while (*p >= '0' && *p <= '9') { id = id * 10 + (uint32_t)(*p - '0'); p++; }
    if (*p != 0 || p == id_str) {
      conn_send_simple(r->conn, 400, NULL, "bad sf id\n", 10, 0);
      return;
    }
  }
  const cerco_sf_entry *ent = NULL;
  for (size_t i = 0; i < srv->app->n_sfs; i++) {
    if (srv->app->sfs[i].id == id) { ent = &srv->app->sfs[i]; break; }
  }
  if (!ent) {
    conn_send_simple(r->conn, 404, NULL, "unknown server function\n", 24, 0);
    return;
  }
  const char *method = cerco_method(r);
  if (!cerco_strcaseeq(method, ent->method) &&
      !(cerco_strcaseeq(method, "HEAD") && cerco_strcaseeq(ent->method, "GET"))) {
    conn_send_simple(r->conn, 405, "Allow: POST", "method not allowed\n", 19, 0);
    return;
  }
  const char *ct = cerco_header(r, "content-type");
  if (!ct || !cerco_strncaseeq(ct, SF_CONTENT_TYPE, sizeof(SF_CONTENT_TYPE) - 1)) {
    conn_send_simple(r->conn, 415, NULL, "expected application/x-cerco-sf\n", 31, 0);
    return;
  }

  r->sf_entry = (void *)ent;
  r->handler = sf_run;
  if (wpool_try_submit(srv, r) != 0) {
    atomic_fetch_add(&srv->stats.rejected_jobs, 1);
    conn_send_simple(r->conn, 503, "Retry-After: 1", "server busy\n", 12, 0);
  }
}

/* worker side: decode frame, call, encode response into r->resp */
void sf_run(cerco_req *r) {
  const cerco_sf_entry *ent = (const cerco_sf_entry *)r->sf_entry;
  cerco_sf_ctx ctx;
  memset(&ctx, 0, sizeof(ctx));
  ctx.req = r;
  cerco_wreader_init(&ctx.reader, r->body ? r->body : "", r->body_len);

  uint32_t arg_count = cerco_r_u32(&ctx.reader);
  if (ctx.reader.err) {
    r->status = 400;
    wbuf_reset(&r->resp);
    wbuf_puts(&r->resp, "truncated sf payload\n");
    return;
  }
  (void)arg_count; /* adapters consume exactly their args */

  ent->call(r, &ctx);
  if (ctx.reader.err) {
    r->status = 400;
    wbuf_reset(&r->resp);
    wbuf_puts(&r->resp, "malformed sf payload\n");
    return;
  }

  cerco_sb out;
  sb_init(&out);
  if (ctx.failed) {
    sb_putc(&out, (char)1);
    const char *msg = ctx.error ? ctx.error : "error";
    sb_put_u32le(&out, (uint32_t)strlen(msg));
    sb_puts(&out, msg);
  } else {
    sb_putc(&out, (char)0);
    sb_put_u32le(&out, 0); /* empty error field */
    sb_put_u32le(&out, 1); /* one value */
    cerco_wval v;
    memset(&v, 0, sizeof(v));
    v.type = ctx.ret_type;
    switch (ctx.ret_type) {
      case CERCO_WT_I32: v.as.i32 = ctx.ret_i32; break;
      case CERCO_WT_I64: v.as.i64 = ctx.ret_i64; break;
      case CERCO_WT_F64: v.as.f64 = ctx.ret_f64; break;
      case CERCO_WT_BOOL: v.as.i32 = ctx.ret_bool ? 1 : 0; break;
      case CERCO_WT_STR:
        v.bytes.data = (const uint8_t *)(ctx.ret_str ? ctx.ret_str : "");
        v.bytes.len = strlen(ctx.ret_str ? ctx.ret_str : "");
        break;
      case CERCO_WT_BYTES: v.bytes = ctx.ret_bytes; break;
      default: break;
    }
    size_t need = cerco_wval_size(&v);
    uint8_t *tmp = (uint8_t *)malloc(need > 0 ? need : 1);
    if (!tmp) { sb_free(&out); r->status = 500; wbuf_reset(&r->resp); wbuf_puts(&r->resp, "oom\n"); return; }
    cerco_wwriter w;
    cerco_wwriter_init(&w, tmp, need);
    cerco_w_val(&w, &v);
    if (!w.err) sb_putn(&out, tmp, need);
    free(tmp);
    if (w.err) { sb_free(&out); r->status = 500; wbuf_reset(&r->resp); wbuf_puts(&r->resp, "encode error\n"); return; }
  }

  r->status = 200;
  cerco_set_header(r, "Content-Type", SF_CONTENT_TYPE);
  cerco_set_header(r, "Cache-Control", "no-store");
  wbuf_reset(&r->resp);
  cerco_write_bytes(r, out.buf, out.len);
  sb_free(&out);
  /* drain (event loop) writes it */
}

/* ------------------------------------------------------- reserved endpoints */

static void send_health(cerco_req *r) {
  r->status = 200;
  cerco_set_header(r, "Content-Type", "text/plain; charset=utf-8");
  cerco_set_header(r, "Cache-Control", "no-store");
  wbuf_reset(&r->resp);
  wbuf_puts(&r->resp, "ok");
  conn_start_response(r);
}

static void send_stats(cerco_req *r) {
  cerco_server *srv = r->conn->srv;
  cerco_stats *s = &srv->stats;
  r->status = 200;
  cerco_set_header(r, "Content-Type", "application/json; charset=utf-8");
  cerco_set_header(r, "Cache-Control", "no-store");
  wbuf_reset(&r->resp);
  wbuf_printf(&r->resp,
      "{\"active_connections\":%ld,\"total_connections\":%ld,"
      "\"active_requests\":%ld,\"total_requests\":%ld,"
      "\"queued_jobs\":%ld,\"completed_jobs\":%ld,\"rejected_jobs\":%ld,"
      "\"bytes_read\":%ld,\"bytes_written\":%ld,"
      "\"responses_2xx\":%ld,\"responses_3xx\":%ld,\"responses_4xx\":%ld,"
      "\"responses_5xx\":%ld,\"arena_high_water\":%ld,"
      "\"event_loop_lag_ms\":%ld,\"sse_clients\":%ld}",
      atomic_load(&s->active_conns), atomic_load(&s->total_conns),
      atomic_load(&s->active_reqs), atomic_load(&s->total_reqs),
      atomic_load(&s->queued_jobs), atomic_load(&s->completed_jobs),
      atomic_load(&s->rejected_jobs), atomic_load(&s->bytes_read),
      atomic_load(&s->bytes_written), atomic_load(&s->r2xx), atomic_load(&s->r3xx),
      atomic_load(&s->r4xx), atomic_load(&s->r5xx), atomic_load(&s->arena_hwm),
      atomic_load(&s->event_loop_lag_ms), atomic_load(&s->sse_clients));
  conn_start_response(r);
}

void conn_sse_write(cerco_conn *c, const char *data, size_t len) {
  char head[32];
  int hn = snprintf(head, sizeof(head), "%zx\r\n", len);
  size_t total = (size_t)hn + len + 2;
  char *buf = (char *)malloc(total);
  if (!buf) { conn_close_and_free(c); return; }
  memcpy(buf, head, (size_t)hn);
  memcpy(buf + hn, data, len);
  buf[hn + len] = '\r';
  buf[hn + len + 1] = '\n';
  write_ctx *ctx = (write_ctx *)malloc(sizeof(write_ctx));
  if (!ctx) { free(buf); conn_close_and_free(c); return; }
  ctx->req = NULL;
  ctx->conn = c;
  ctx->buf = buf;
  uv_buf_t b = uv_buf_init(buf, (unsigned int)total);
  c->pending_writes++;
  int rc = uv_write(&ctx->wr, (uv_stream_t *)&c->stream, &b, 1, conn_on_sse_written);
  if (rc) { c->pending_writes--; free(ctx->buf); free(ctx); conn_close_and_free(c); }
}

void conn_on_sse_written(uv_write_t *w, int status) {
  write_ctx *ctx = (write_ctx *)w;
  cerco_conn *c = ctx->conn;
  free(ctx->buf);
  free(ctx);
  c->pending_writes--;
  if (status && !c->closing && !c->finalized) conn_close_and_free(c);
  else conn_maybe_finalize(c);
}

static void send_live_sse(cerco_req *r) {
  cerco_conn *c = r->conn;
  cerco_server *srv = c->srv;
  const char *hdr =
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: text/event-stream\r\n"
      "Cache-Control: no-store\r\n"
      "Connection: keep-alive\r\n"
      "Server: cerco\r\n\r\n";
  size_t hl = strlen(hdr);
  char *buf = (char *)malloc(hl);
  if (!buf) { conn_close_and_free(c); return; }
  memcpy(buf, hdr, hl);
  write_ctx *ctx = (write_ctx *)malloc(sizeof(write_ctx));
  if (!ctx) { free(buf); conn_close_and_free(c); return; }
  ctx->req = NULL;
  ctx->conn = c;
  ctx->buf = buf;
  uv_buf_t b = uv_buf_init(buf, (unsigned int)hl);
  c->pending_writes++;
  int rc = uv_write(&ctx->wr, (uv_stream_t *)&c->stream, &b, 1, conn_on_sse_written);
  if (rc) {
    c->pending_writes--;
    free(ctx->buf);
    free(ctx);
    conn_close_and_free(c);
    return;
  }
  c->sse = 1;
  c->sse_prev = NULL;
  c->sse_next = srv->sse_clients;
  if (srv->sse_clients) srv->sse_clients->sse_prev = c;
  srv->sse_clients = c;
  atomic_fetch_add(&srv->stats.sse_clients, 1);
  atomic_fetch_sub(&srv->stats.active_reqs, 1);
  conn_sse_write(c, ":ok\n\n", 5);
}

static void send_live_js(cerco_req *r) {
  static const char js[] =
      "new EventSource('/__cerco/live').onmessage=function(){location.reload();};";
  r->status = 200;
  cerco_set_header(r, "Content-Type", "text/javascript; charset=utf-8");
  cerco_set_header(r, "Cache-Control", "no-store");
  wbuf_reset(&r->resp);
  wbuf_puts(&r->resp, js);
  conn_start_response(r);
}

void sf_handle_reserved(cerco_req *r) {
  cerco_server *srv = r->conn->srv;
  const char *path = r->path;
  if (cerco_strcaseeq(path, "/__cerco/health")) {
    send_health(r);
  } else if (cerco_strcaseeq(path, "/__cerco/stats")) {
    if (!srv->cfg.stats_enabled) router_send_404(r);
    else send_stats(r);
  } else if (cerco_strcaseeq(path, "/__cerco/live")) {
    if (!srv->cfg.dev_mode) router_send_404(r);
    else send_live_sse(r);
  } else if (cerco_strcaseeq(path, "/__cerco/live.js")) {
    if (!srv->cfg.dev_mode) router_send_404(r);
    else send_live_js(r);
  } else {
    router_send_404(r);
  }
}

void dev_inject_live_script(cerco_req *r) {
  cerco_server *srv = r->conn->srv;
  if (!srv->cfg.dev_mode || r->head_injected || r->status != 200) return;
  const char *ct = NULL;
  for (cerco_resp_hdr *h = r->resp_hdrs; h; h = h->next)
    if (cerco_strcaseeq(h->name, "content-type")) { ct = h->value; break; }
  /* no explicit content-type: conn_start_response defaults to text/html */
  if (ct && !cerco_strncaseeq(ct, "text/html", 9)) return;

  static const char script[] = "<script src=\"/assets/host.js\" defer></script>"
                               "<script src=\"/__cerco/live.js\" defer></script>";
  const char *head = r->resp.data ? strstr(r->resp.data, "<head>") : NULL;
  size_t insert_at = head ? (size_t)(head - r->resp.data + 6) : 0;
  size_t script_len = sizeof(script) - 1;
  size_t new_len = r->resp.len + script_len;
  char *nb = (char *)cerco_arena_alloc(r->arena, new_len);
  if (!nb) return;
  memcpy(nb, r->resp.data, insert_at);
  memcpy(nb + insert_at, script, script_len);
  memcpy(nb + insert_at + script_len, r->resp.data + insert_at,
         r->resp.len - insert_at);
  r->resp.data = nb;
  r->resp.len = new_len;
  r->resp.cap = new_len;
  r->head_injected = 1;
}
