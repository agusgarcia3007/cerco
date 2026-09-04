/* SSR writer: HTML is written incrementally into the arena-backed response
 * buffer. Escaping is on by default; raw output is explicit (cerco_raw). */
#include "internal.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

/* ---------------------------------------------------------------- headers */

void cerco_status(cerco_req *r, int code) {
  if (code >= 100 && code <= 599) r->status = code;
}

static void resp_hdr_add(cerco_req *r, const char *name, const char *value) {
  if (r->resp_hdrs_count >= 64) return;
  cerco_resp_hdr *h = (cerco_resp_hdr *)cerco_arena_alloc(r->arena, sizeof(*h));
  if (!h) return;
  h->name = cerco_arena_strdup(r->arena, name);
  h->value = cerco_arena_strdup(r->arena, value);
  if (!h->name || !h->value) return;
  h->next = NULL;
  if (r->resp_hdrs_tail) r->resp_hdrs_tail->next = h;
  else r->resp_hdrs = h;
  r->resp_hdrs_tail = h;
  r->resp_hdrs_count++;
}

void cerco_set_header(cerco_req *r, const char *name, const char *value) {
  /* replace existing (case-insensitive) */
  for (cerco_resp_hdr *h = r->resp_hdrs; h; h = h->next) {
    if (cerco_strcaseeq(h->name, name)) {
      h->value = cerco_arena_strdup(r->arena, value);
      return;
    }
  }
  resp_hdr_add(r, name, value);
}

void cerco_add_header(cerco_req *r, const char *name, const char *value) {
  resp_hdr_add(r, name, value);
}

void cerco_content_type(cerco_req *r, const char *ct) {
  cerco_set_header(r, "Content-Type", ct);
}

void cerco_set_cookie(cerco_req *r, const char *name, const char *value,
                      int max_age, const char *path, unsigned flags) {
  cerco_sb sb;
  sb_init(&sb);
  sb_puts(&sb, name);
  sb_puts(&sb, "=");
  sb_puts(&sb, value);
  if (max_age >= 0) sb_printf(&sb, "; Max-Age=%d", max_age);
  sb_puts(&sb, "; Path=");
  sb_puts(&sb, path ? path : "/");
  if (flags & CERCO_COOKIE_HTTPONLY) sb_puts(&sb, "; HttpOnly");
  if (flags & CERCO_COOKIE_SECURE) sb_puts(&sb, "; Secure");
  if (flags & CERCO_COOKIE_SAMESITE_LAX) sb_puts(&sb, "; SameSite=Lax");
  if (flags & CERCO_COOKIE_SAMESITE_STRICT) sb_puts(&sb, "; SameSite=Strict");
  if (flags & CERCO_COOKIE_SAMESITE_NONE) sb_puts(&sb, "; SameSite=None");
  cerco_add_header(r, "Set-Cookie", sb.buf ? sb.buf : "");
  sb_free(&sb);
}

void cerco_redirect(cerco_req *r, const char *location) {
  r->status = cerco_strcaseeq(cerco_method(r), "POST") ? 303 : 302;
  cerco_set_header(r, "Location", location);
  wbuf_reset(&r->resp);
  wbuf_puts(&r->resp, "redirecting...");
  r->responded = 1;
}

/* ------------------------------------------------------------- html writes */

/* append escaped text into resp */
static void wbuf_escape(cerco_wbuf *b, const char *s, size_t len) {
  size_t start = 0;
  for (size_t i = 0; i < len; i++) {
    const char *rep = NULL;
    size_t rlen = 0;
    switch (s[i]) {
      case '&': rep = "&amp;"; rlen = 5; break;
      case '<': rep = "&lt;"; rlen = 4; break;
      case '>': rep = "&gt;"; rlen = 4; break;
      case '"': rep = "&quot;"; rlen = 6; break;
      case '\'': rep = "&#39;"; rlen = 5; break;
      default: continue;
    }
    if (i > start) wbuf_putn(b, s + start, i - start);
    wbuf_putn(b, rep, rlen);
    start = i + 1;
  }
  if (len > start) wbuf_putn(b, s + start, len - start);
}

void cerco_text(cerco_req *r, const char *s) {
  if (s) wbuf_escape(&r->resp, s, strlen(s));
}

void cerco_textf(cerco_req *r, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const char *p = fmt;
  while (*p) {
    if (*p == '%') {
      p++;
      if (*p == '%') { wbuf_putc(&r->resp, '%'); p++; continue; }
      /* build spec: flags/width/precision/length */
      char spec[40];
      size_t si = 0;
      spec[si++] = '%';
      const char *conv = NULL;
      char lenmod = 0;
      while (*p && si < sizeof(spec) - 2) {
        char ch = *p;
        if (strchr("diouxXeEfgGcs", ch)) { conv = spec + si; spec[si++] = ch; p++; break; }
        if (ch == 'l' || ch == 'z' || ch == 'h') lenmod = ch;
        spec[si++] = ch;
        p++;
      }
      if (!conv) break;
      spec[si] = 0;
      char c = *conv;
      if (c == 's') {
        const char *sv = va_arg(ap, const char *);
        if (sv) wbuf_escape(&r->resp, sv, strlen(sv));
        else wbuf_puts(&r->resp, "(null)");
      } else if (c == 'f' || c == 'g' || c == 'e' || c == 'G' || c == 'E') {
        char tmp[160];
        int n = snprintf(tmp, sizeof(tmp), spec, va_arg(ap, double));
        if (n > 0) wbuf_putn(&r->resp, tmp, (size_t)(n > 159 ? 159 : n));
      } else {
        char tmp[160];
        int n;
        if (lenmod == 'l') {
          /* %ld/%lld/%lu/%llu */
          int ll = (conv > spec + 1 && conv[-1] == 'l');
          if (c == 'u' || c == 'x' || c == 'X' || c == 'o')
            n = ll ? snprintf(tmp, sizeof(tmp), spec, va_arg(ap, unsigned long long))
                   : snprintf(tmp, sizeof(tmp), spec, va_arg(ap, unsigned long));
          else
            n = ll ? snprintf(tmp, sizeof(tmp), spec, va_arg(ap, long long))
                   : snprintf(tmp, sizeof(tmp), spec, va_arg(ap, long));
        } else if (lenmod == 'z') {
          n = snprintf(tmp, sizeof(tmp), spec, va_arg(ap, size_t));
        } else if (c == 'u' || c == 'x' || c == 'X' || c == 'o') {
          n = snprintf(tmp, sizeof(tmp), spec, va_arg(ap, unsigned int));
        } else if (c == 'c') {
          n = snprintf(tmp, sizeof(tmp), spec, va_arg(ap, int));
        } else {
          n = snprintf(tmp, sizeof(tmp), spec, va_arg(ap, int));
        }
        if (n > 0) wbuf_putn(&r->resp, tmp, (size_t)(n > 159 ? 159 : n));
      }
    } else {
      wbuf_putc(&r->resp, *p++);
    }
  }
  va_end(ap);
}

void cerco_raw(cerco_req *r, const char *s) { wbuf_puts(&r->resp, s); }

/* Per-page <title>.
 *
 * A layout streams its <head> before the route body runs, so a route cannot
 * write into it directly. Instead it records the title here and the response
 * finalizer rewrites the layout's <title> element once the document is
 * complete — the same after-the-fact edit the dev live-reload script uses. */
void cerco_title(cerco_req *r, const char *title) {
  if (!r || !title) return;
  size_t need = cerco_html_escape(title, strlen(title), NULL, 0);
  char *buf = (char *)cerco_arena_alloc(r->arena, need + 1);
  if (!buf) return;
  cerco_html_escape(title, strlen(title), buf, need + 1);
  r->page_title = buf;
}

/* replace the contents of the document's first <title> element */
void apply_page_title(cerco_req *r) {
  /* no status filter: a 404 page is still a page. Bodies without a <title>
   * (redirects, JSON, plain text) fall out on the search below. */
  if (!r->page_title || !r->resp.data) return;
  char *open = wbuf_find(&r->resp, 0, "<title>");
  if (!open) return;
  char *inner = open + 7;
  char *close = wbuf_find(&r->resp, (size_t)(inner - r->resp.data), "</title>");
  if (!close) return;

  size_t head = (size_t)(inner - r->resp.data);
  size_t tail_at = (size_t)(close - r->resp.data);
  size_t tail_len = r->resp.len - tail_at;
  size_t title_len = strlen(r->page_title);
  size_t new_len = head + title_len + tail_len;

  char *nb = (char *)cerco_arena_alloc(r->arena, new_len);
  if (!nb) return;
  memcpy(nb, r->resp.data, head);
  memcpy(nb + head, r->page_title, title_len);
  memcpy(nb + head + title_len, r->resp.data + tail_at, tail_len);
  r->resp.data = nb;
  r->resp.len = new_len;
  r->resp.cap = new_len;
}

void cerco_write_bytes(cerco_req *r, const void *data, size_t len) {
  wbuf_putn(&r->resp, data, len);
}

const char *cerco_prop_escape(cerco_req *r, const char *json) {
  if (!json) return "";
  size_t need = cerco_html_escape(json, strlen(json), NULL, 0);
  char *buf = (char *)cerco_arena_alloc(r->arena, need + 1);
  if (!buf) return "";
  cerco_html_escape(json, strlen(json), buf, need + 1);
  return buf;
}

/* ----------------------------------------------------------------- tags */

void cerco_tag_open(cerco_req *r, const char *tag, const cerco_attr *attrs) {
  wbuf_putc(&r->resp, '<');
  wbuf_puts(&r->resp, tag);
  if (attrs) {
    for (const cerco_attr *a = attrs; a->k; a++) {
      wbuf_putc(&r->resp, ' ');
      wbuf_puts(&r->resp, a->k);
      wbuf_puts(&r->resp, "=\"");
      if (a->v) wbuf_escape(&r->resp, a->v, strlen(a->v));
      wbuf_putc(&r->resp, '"');
    }
  }
  wbuf_putc(&r->resp, '>');
}

void cerco_tag_close(cerco_req *r, const char *tag) {
  wbuf_puts(&r->resp, "</");
  wbuf_puts(&r->resp, tag);
  wbuf_putc(&r->resp, '>');
}

void cerco_void_tag(cerco_req *r, const char *tag, const cerco_attr *attrs) {
  wbuf_putc(&r->resp, '<');
  wbuf_puts(&r->resp, tag);
  if (attrs) {
    for (const cerco_attr *a = attrs; a->k; a++) {
      wbuf_putc(&r->resp, ' ');
      wbuf_puts(&r->resp, a->k);
      wbuf_puts(&r->resp, "=\"");
      if (a->v) wbuf_escape(&r->resp, a->v, strlen(a->v));
      wbuf_putc(&r->resp, '"');
    }
  }
  wbuf_putc(&r->resp, '>');
}
