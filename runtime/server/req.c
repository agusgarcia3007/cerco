/* request object: lifecycle + lazy parsing (query, cookies, form, json field) */
#include "internal.h"
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>

void req_setup(cerco_req *r, cerco_conn *c) {
  memset(r, 0, sizeof(*r));
  r->conn = c;
  r->arena = &c->arena;
  r->status = 200;
  atomic_store(&r->cancelled, 0);
  wbuf_init(&r->resp, r->arena);
}

void req_reset(cerco_req *r) {
  /* arena is reset by the event loop; just drop pointers */
  memset(r, 0, sizeof(*r));
  r->status = 200;
  r->conn = NULL;
  atomic_store(&r->cancelled, 0);
}

/* commit accumulated field/value staging into the header list */
void req_finish_headers(cerco_conn *c) {
  if (!c->req) return;
  cerco_req *r = c->req;
  if (c->f_sb.len == 0) { c->in_value = 0; return; }
  if (r->n_headers >= CERCO_MAX_HEADERS) { c->bad_request = 431; return; }
  if (c->f_sb.len > CERCO_MAX_NAME || c->v_sb.len > CERCO_MAX_VALUE ||
      c->f_sb.len + c->v_sb.len + r->n_headers * 2 > CERCO_MAX_HEADER_BYTES) {
    c->bad_request = 431;
    return;
  }
  char *name = cerco_arena_strndup(r->arena, c->f_sb.buf, c->f_sb.len);
  char *value = cerco_arena_strndup(r->arena, c->v_sb.buf, c->v_sb.len);
  if (!name || !value) return;
  r->headers[r->n_headers].name = name;
  r->headers[r->n_headers].value = value;
  r->n_headers++;
  c->f_sb.len = 0;
  c->v_sb.len = 0;
  c->in_value = 0;
  if (c->f_sb.buf) c->f_sb.buf[0] = 0;
  if (c->v_sb.buf) c->v_sb.buf[0] = 0;
}

/* decode r->target into r->path + r->query (arena-owned).
 * returns 0 on success, otherwise the HTTP error code (414/400). */
int req_target_path(cerco_req *r) {
  if (r->path) return 0;
  cerco_arena *a = r->arena;
  const char *t = r->target ? r->target : "/";
  const char *qm = strchr(t, '?');
  size_t plen = qm ? (size_t)(qm - t) : strlen(t);
  /* absolute-form "http://host/path" -> cut scheme+host */
  if (plen > 7 && cerco_strncaseeq(t, "http://", 7)) {
    const char *slash = memchr(t + 7, '/', plen - 7);
    if (slash) { plen -= (size_t)(slash - t); t = slash; }
    else plen = 1, t = "/";
  }
  if (plen == 0) { t = "/"; plen = 1; }
  if (plen > CERCO_MAX_URL) return 414;
  char *decoded = (char *)cerco_arena_alloc(a, plen + 1);
  if (!decoded) return 400;
  size_t dl = cerco_url_decode(t, plen, decoded, plen + 1, 0);
  if (dl == (size_t)-1) return 400; /* strict: reject bad escapes */
  r->path = decoded;
  if (qm) {
    /* strictly validate the query string too: bad escapes reject the request */
    size_t qlen = strlen(qm + 1);
    char *qdec = (char *)cerco_arena_alloc(a, qlen + 1);
    if (!qdec) return 400;
    if (cerco_url_decode(qm + 1, qlen, qdec, qlen + 1, 1) == (size_t)-1) return 400;
    r->query = qdec;
  } else {
    r->query = (char *)"";
  }
  return 0;
}

/* --------------------------------------------------------------- public API */

const char *cerco_method(cerco_req *r) { return r->method ? r->method : ""; }

const char *cerco_path(cerco_req *r) { return r->path ? r->path : "/"; }


const char *cerco_query(cerco_req *r) {
  if (!r->query) req_target_path(r);
  return r->query ? r->query : "";
}

const char *cerco_param(cerco_req *r, const char *name) {
  for (int i = 0; i < r->n_route_params; i++)
    if (cerco_strcaseeq(r->route_params_name[i], name)) return r->route_params_value[i];
  return NULL;
}

const char *cerco_query_get(cerco_req *r, const char *name) {
  const char *q = cerco_query(r);
  size_t pos = 0;
  char kbuf[256], vbuf[2048];
  while (cerco_query_next(q, &pos, kbuf, sizeof(kbuf), vbuf, sizeof(vbuf)) == 1) {
    if (cerco_strcaseeq(kbuf, name)) return cerco_arena_strdup(r->arena, vbuf);
  }
  return NULL;
}

const char *cerco_header(cerco_req *r, const char *name) {
  for (int i = 0; i < r->n_headers; i++)
    if (cerco_strcaseeq(r->headers[i].name, name)) return r->headers[i].value;
  return NULL;
}

const char *cerco_cookie(cerco_req *r, const char *name) {
  const char *hv = cerco_header(r, "cookie");
  if (!hv) return NULL;
  const char *p = hv;
  while (*p) {
    while (*p == ' ' || *p == '\t') p++;
    const char *eq = strchr(p, '=');
    const char *semi = strchr(p, ';');
    if (!semi) semi = p + strlen(p);
    if (eq && eq < semi) {
      size_t klen = (size_t)(eq - p);
      if ((int)klen < CERCO_MAX_NAME &&
          cerco_strncaseeq(p, name, klen) && strlen(name) == klen) {
        size_t vlen = (size_t)(semi - (eq + 1));
        /* trim quotes */
        if (vlen >= 2 && eq[1] == '"' && eq[vlen] == '"') {
          eq++; vlen -= 2;
        }
        char *v = (char *)cerco_arena_alloc(r->arena, vlen + 1);
        if (!v) return NULL;
        memcpy(v, eq + 1, vlen);
        v[vlen] = 0;
        return v;
      }
    }
    p = *semi ? semi + 1 : semi;
  }
  return NULL;
}

const char *cerco_body(cerco_req *r, size_t *len) {
  if (len) *len = r->body_len;
  if (!r->body) return NULL;
  return r->body;
}

/* parse urlencoded body into form list (once) */
static void form_parse(cerco_req *r) {
  if (r->form) return;
  const char *ct = cerco_header(r, "content-type");
  if (!ct) return;
  if (!cerco_strncaseeq(ct, "application/x-www-form-urlencoded", 33)) return;
  if (!r->body) return;
  cerco_form_kv **tail = &r->form;
  size_t pos = 0;
  const char *b = r->body;
  size_t blen = r->body_len;
  /* cerco_query_next wants NUL-terminated input; body may contain NULs from
   * binary bodies, so use a bounded manual walk instead */
  while (pos < blen) {
    size_t kstart = pos;
    while (pos < blen && b[pos] != '=' && b[pos] != '&') pos++;
    char kbuf[256], vbuf[2048];
    size_t klen = pos - kstart;
    size_t vlen = 0;
    const char *vsrc = "";
    if (pos < blen && b[pos] == '=') {
      pos++;
      vsrc = b + pos;
      while (pos < blen && b[pos] != '&') pos++;
      vlen = (size_t)(pos - (vsrc - b));
    }
    if (pos < blen && b[pos] == '&') pos++;
    if (cerco_url_decode(b + kstart, klen, kbuf, sizeof(kbuf), 1) == (size_t)-1) return;
    if (cerco_url_decode(vsrc, vlen, vbuf, sizeof(vbuf), 1) == (size_t)-1) return;
    cerco_form_kv *kv = (cerco_form_kv *)cerco_arena_alloc(r->arena, sizeof(*kv));
    if (!kv) return;
    kv->name = cerco_arena_strdup(r->arena, kbuf);
    kv->value = cerco_arena_strdup(r->arena, vbuf);
    kv->next = NULL;
    *tail = kv;
    tail = &kv->next;
  }
}

const char *cerco_form(cerco_req *r, const char *name) {
  form_parse(r);
  for (cerco_form_kv *kv = r->form; kv; kv = kv->next)
    if (cerco_strcaseeq(kv->name, name)) return kv->value;
  return NULL;
}

const char *cerco_remote_addr(cerco_req *r) {
  cerco_conn *c = r->conn;
  if (!c) return "";
  struct sockaddr_storage ss;
  int slen = sizeof(ss);
  if (uv_tcp_getpeername(&c->stream, (struct sockaddr *)&ss, &slen) != 0) return "";
  char host[64];
  if (ss.ss_family == AF_INET) {
    struct sockaddr_in *in = (struct sockaddr_in *)&ss;
    uv_ip4_name(in, host, sizeof(host));
    return cerco_arena_sprintf(r->arena, "%s:%d", host, (int)ntohs(in->sin_port));
  } else if (ss.ss_family == AF_INET6) {
    struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&ss;
    uv_ip6_name(in6, host, sizeof(host));
    return cerco_arena_sprintf(r->arena, "[%s]:%d", host, (int)ntohs(in6->sin6_port));
  }
  return "";
}

/* ---- tiny json string-field extractor (flat objects, good enough for SF helpers) */
const char *cerco_json_string(cerco_req *r, const char *field) {
  const char *body = cerco_body(r, NULL);
  if (!body) return NULL;
  size_t flen = strlen(field);
  const char *p = body;
  while ((p = strstr(p, field)) != NULL) {
    if (p != body && p[-1] == '"') { p += flen; continue; } /* substring of longer key */
    const char *q = p + flen;
    /* expect ": " or ":" then '"' */
    while (*q == ' ' || *q == '\t') q++;
    if (*q != ':') { p += flen; continue; }
    q++;
    while (*q == ' ' || *q == '\t') q++;
    if (*q != '"') { p += flen; continue; }
    q++;
    cerco_sb sb;
    sb_init(&sb);
    while (*q && *q != '"') {
      if (*q == '\\' && q[1]) {
        char c = q[1];
        q += 2;
        switch (c) {
          case 'n': sb_putc(&sb, '\n'); break;
          case 't': sb_putc(&sb, '\t'); break;
          case 'r': sb_putc(&sb, '\r'); break;
          case '"': sb_putc(&sb, '"'); break;
          case '\\': sb_putc(&sb, '\\'); break;
          case '/': sb_putc(&sb, '/'); break;
          case 'u': { /* \uXXXX: passthrough as UTF-8 for basic planes */
            unsigned cp = 0;
            for (int i = 0; i < 4 && *q; i++, q++) {
              char h = *q;
              cp <<= 4;
              if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
              else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
              else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
              else { sb_free(&sb); return NULL; }
            }
            if (cp < 0x80) {
              sb_putc(&sb, (char)cp);
            } else if (cp < 0x800) {
              sb_putc(&sb, (char)(0xC0 | (cp >> 6)));
              sb_putc(&sb, (char)(0x80 | (cp & 0x3F)));
            } else {
              sb_putc(&sb, (char)(0xE0 | (cp >> 12)));
              sb_putc(&sb, (char)(0x80 | ((cp >> 6) & 0x3F)));
              sb_putc(&sb, (char)(0x80 | (cp & 0x3F)));
            }
            break;
          }
          default: sb_free(&sb); return NULL;
        }
      } else {
        sb_putc(&sb, *q++);
      }
    }
    if (*q != '"') { sb_free(&sb); return NULL; }
    char *out = sb.buf ? cerco_arena_strndup(r->arena, sb.buf, sb.len) : (char *)"";
    sb_free(&sb);
    return out;
  }
  return NULL;
}

/* ---- sf ctx ---- */
void cerco_sf_fail(cerco_sf_ctx *ctx, const char *message) {
  ctx->failed = 1;
  ctx->error = message;
}

void *cerco_sf_arena(cerco_sf_ctx *ctx) { return ctx->req ? ctx->req->arena : NULL; }

int cerco_cancelled(cerco_req *r) { return atomic_load(&r->cancelled); }
