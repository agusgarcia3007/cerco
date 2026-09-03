#include "internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

int64_t cerco_now_ms(void) {
  struct timespec ts;
#ifdef CLOCK_MONOTONIC
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#else
  clock_gettime(CLOCK_REALTIME, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
#endif
}

int cerco_cpu_count(void) {
  uv_cpu_info_t *info;
  int count;
  if (uv_cpu_info(&info, &count) != 0) return 1;
  uv_free_cpu_info(info, count);
  return count > 0 ? count : 1;
}

const char *cerco_status_text(int code) {
  switch (code) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
    default: return "";
  }
}

/* ---- arena-backed response buffer ---- */

void wbuf_init(cerco_wbuf *b, cerco_arena *a) {
  b->a = a;
  b->data = NULL;
  b->len = 0;
  b->cap = 0;
}

static void wbuf_grow(cerco_wbuf *b, size_t need) {
  if (b->len + need <= b->cap) return;
  size_t cap = b->cap ? b->cap * 2 : 4096;
  while (cap < b->len + need) cap *= 2;
  char *nd = cerco_arena_alloc(b->a, cap);
  if (!nd) return; /* arena OOM already flagged */
  if (b->len) memcpy(nd, b->data, b->len);
  b->data = nd;
  b->cap = cap;
}

void wbuf_putn(cerco_wbuf *b, const void *data, size_t len) {
  wbuf_grow(b, len);
  if (b->cap < b->len + len) return;
  memcpy(b->data + b->len, data, len);
  b->len += len;
}

void wbuf_puts(cerco_wbuf *b, const char *s) { wbuf_putn(b, s, strlen(s)); }

void wbuf_putc(cerco_wbuf *b, char c) { wbuf_putn(b, &c, 1); }

void wbuf_reset(cerco_wbuf *b) { b->data = NULL; b->len = 0; b->cap = 0; }

void wbuf_printf(cerco_wbuf *b, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n >= 0) {
    wbuf_grow(b, (size_t)n);
    if (b->cap >= b->len + (size_t)n) {
      vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap2);
      b->len += (size_t)n;
    }
  }
  va_end(ap2);
}
