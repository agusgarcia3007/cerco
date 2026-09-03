#include "str.h"
#include <string.h>

size_t cerco_html_escape(const char *src, size_t len, char *buf, size_t cap) {
  /* worst case: every char is '&' -> "&amp;" (5) ; allocate accordingly */
  size_t need = 0;
  if (buf && cap == 0) return 0;
  if (buf) {
    size_t o = 0;
    for (size_t i = 0; i < len; i++) {
      const char *rep = NULL;
      size_t rlen = 0;
      char c = src[i];
      switch (c) {
        case '&': rep = "&amp;"; rlen = 5; break;
        case '<': rep = "&lt;"; rlen = 4; break;
        case '>': rep = "&gt;"; rlen = 4; break;
        case '"': rep = "&quot;"; rlen = 6; break;
        case '\'': rep = "&#39;"; rlen = 5; break;
        default: break;
      }
      if (rep) {
        if (o + rlen > cap - 1) return (size_t)-1; /* does not fit */
        memcpy(buf + o, rep, rlen);
        o += rlen;
      } else {
        if (o + 1 > cap - 1) return (size_t)-1;
        buf[o++] = c;
      }
    }
    buf[o] = 0;
    return o;
  }
  for (size_t i = 0; i < len; i++) {
    switch (src[i]) {
      case '&': need += 5; break;
      case '<': case '>': need += 4; break;
      case '"': need += 6; break;
      case '\'': need += 5; break;
      default: need += 1; break;
    }
  }
  return need;
}

static int hexval(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

size_t cerco_url_decode(const char *src, size_t len, char *dst, size_t dstcap,
                        int plus_is_space) {
  size_t o = 0;
  if (dst && dstcap == 0) return (size_t)-1;
  for (size_t i = 0; i < len; i++) {
    char out;
    char c = src[i];
    if (c == '%' ) {
      if (i + 2 >= len) return (size_t)-1; /* truncated escape: strict reject */
      int hi = hexval(src[i + 1]);
      int lo = hexval(src[i + 2]);
      if (hi < 0 || lo < 0) return (size_t)-1;
      out = (char)((hi << 4) | lo);
      i += 2;
    } else if (c == '+' && plus_is_space) {
      out = ' ';
    } else {
      out = c;
    }
    if (o + 1 >= dstcap) return (size_t)-1; /* too long */
    dst[o++] = out;
  }
  if (dst) dst[o] = 0;
  return o;
}

int cerco_query_next(const char *q, size_t *pos, char *kbuf, size_t kcap,
                     char *vbuf, size_t vcap) {
  if (!q) return 0;
  size_t p = *pos;
  if (q[p] == 0) return 0;
  /* key up to '=' or '&' */
  size_t kstart = p;
  while (q[p] && q[p] != '=' && q[p] != '&') p++;
  size_t klen = p - kstart;
  if (cerco_url_decode(q + kstart, klen, kbuf, kcap, 1) == (size_t)-1) return -1;
  size_t vlen = 0;
  const char *vstart = NULL;
  if (q[p] == '=') {
    p++;
    vstart = q + p;
    while (q[p] && q[p] != '&') p++;
    vlen = (size_t)(q + p - vstart);
  }
  if (q[p] == '&') p++;
  *pos = p;
  if (vstart) {
    if (cerco_url_decode(vstart, vlen, vbuf, vcap, 1) == (size_t)-1) return -1;
  } else {
    vbuf[0] = 0;
  }
  return 1;
}

/* sorted-ish common table; linear scan fine (n < 20) */
static const struct { const char *ext, *mime; } mime_table[] = {
  { "html", "text/html; charset=utf-8" },
  { "htm",  "text/html; charset=utf-8" },
  { "css",  "text/css; charset=utf-8" },
  { "js",   "text/javascript; charset=utf-8" },
  { "mjs",  "text/javascript; charset=utf-8" },
  { "json", "application/json; charset=utf-8" },
  { "txt",  "text/plain; charset=utf-8" },
  { "xml",  "application/xml" },
  { "wasm", "application/wasm" },
  { "png",  "image/png" },
  { "jpg",  "image/jpeg" },
  { "jpeg", "image/jpeg" },
  { "gif",  "image/gif" },
  { "svg",  "image/svg+xml" },
  { "webp", "image/webp" },
  { "ico",  "image/x-icon" },
  { "woff", "font/woff" },
  { "woff2","font/woff2" },
  { "ttf",  "font/ttf" },
  { "otf",  "font/otf" },
  { "mp4",  "video/mp4" },
  { "webm", "video/webm" },
  { "pdf",  "application/pdf" },
  { "map",  "application/json" },
};

const char *cerco_mime_from_ext(const char *filename) {
  const char *dot = strrchr(filename, '.');
  if (!dot) return "application/octet-stream";
  dot++;
  for (size_t i = 0; i < sizeof(mime_table) / sizeof(mime_table[0]); i++)
    if (cerco_strcaseeq(dot, mime_table[i].ext)) return mime_table[i].mime;
  return "application/octet-stream";
}

void cerco_hex_encode(const uint8_t *src, size_t len, char *dst) {
  static const char hexd[] = "0123456789abcdef";
  for (size_t i = 0; i < len; i++) {
    dst[i * 2] = hexd[src[i] >> 4];
    dst[i * 2 + 1] = hexd[src[i] & 0xf];
  }
  dst[len * 2] = 0;
}

int cerco_hex_decode(const char *src, size_t len, uint8_t *dst) {
  if (len % 2) return 0;
  for (size_t i = 0; i < len; i += 2) {
    int hi = hexval(src[i]), lo = hexval(src[i + 1]);
    if (hi < 0 || lo < 0) return 0;
    dst[i / 2] = (uint8_t)((hi << 4) | lo);
  }
  return 1;
}

int cerco_strcaseeq(const char *a, const char *b) {
  while (*a && *b) {
    char ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
    char cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
    if (ca != cb) return 0;
    a++; b++;
  }
  return *a == *b;
}

int cerco_strncaseeq(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    char ca = a[i] >= 'A' && a[i] <= 'Z' ? a[i] + 32 : a[i];
    char cb = b[i] >= 'A' && b[i] <= 'Z' ? b[i] + 32 : b[i];
    if (ca != cb) return 0;
  }
  return 1;
}
