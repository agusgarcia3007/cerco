#include "sb.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

static int sb_grow(cerco_sb *b, size_t need) {
  if (b->len + need <= b->cap) return 1;
  size_t cap = b->cap ? b->cap : 256;
  while (cap < b->len + need) cap *= 2;
  char *nb = (char *)realloc(b->buf, cap);
  if (!nb) { b->oom = 1; return 0; }
  b->buf = nb;
  b->cap = cap;
  return 1;
}

void sb_init(cerco_sb *b) { memset(b, 0, sizeof(*b)); }

void sb_reserve(cerco_sb *b, size_t extra) { sb_grow(b, extra); }

void sb_putn(cerco_sb *b, const void *data, size_t len) {
  if (!sb_grow(b, len + 1)) return;
  memcpy(b->buf + b->len, data, len);
  b->len += len;
  b->buf[b->len] = 0;
}

void sb_puts(cerco_sb *b, const char *s) { if (s) sb_putn(b, s, strlen(s)); }

void sb_putc(cerco_sb *b, char c) {
  if (!sb_grow(b, 2)) return;
  b->buf[b->len++] = c;
  b->buf[b->len] = 0;
}

void sb_printf(cerco_sb *b, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n < 0) { va_end(ap2); return; }
  if (sb_grow(b, (size_t)n + 1)) {
    vsnprintf(b->buf + b->len, (size_t)n + 1, fmt, ap2);
    b->len += (size_t)n;
  }
  va_end(ap2);
}

static const char digit_pairs[] =
  "000102030405060708091011121314151617181920212223242526272829"
  "303132333435363738394041424344454647484950515253545556575859"
  "606162636465666768697071727374757677787980818283848586878889"
  "90919293949596979899";

void sb_putu(cerco_sb *b, unsigned long long v) {
  char tmp[24];
  int i = 22;
  tmp[23] = 0;
  while (v >= 100) {
    unsigned rem = (unsigned)(v % 100);
    v /= 100;
    tmp[i - 1] = digit_pairs[rem * 2];
    tmp[i] = digit_pairs[rem * 2 + 1];
    i -= 2;
  }
  while (v >= 10) {
    unsigned rem = (unsigned)(v % 10);
    tmp[i--] = (char)('0' + rem);
    v /= 10;
  }
  tmp[i--] = (char)('0' + v);
  sb_putn(b, tmp + i + 1, (size_t)(22 - i));
}

void sb_puti(cerco_sb *b, long long v) {
  if (v < 0) { sb_putc(b, '-'); sb_putu(b, (unsigned long long)(-(v + 1)) + 1); }
  else sb_putu(b, (unsigned long long)v);
}

char *sb_release(cerco_sb *b) {
  char *p = b->buf;
  b->buf = NULL;
  b->len = b->cap = 0;
  return p;
}

void sb_reset(cerco_sb *b) { b->len = 0; if (b->buf) b->buf[0] = 0; }

void sb_free(cerco_sb *b) {
  free(b->buf);
  memset(b, 0, sizeof(*b));
}
