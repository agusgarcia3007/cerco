/* cerco client runtime — wasm environment: heap, scratch, host imports.
 *
 * Freestanding: provides the compiler's implicit string/mem routines.
 * Heap layout: bump allocator after __heap_base; grows memory in pages up to
 * CERCO_MAX_HEAP_PAGES (default 64 = 4 MiB) so client RAM is bounded.
 */
#include "cerco_client.h"
#include <stdarg.h>
#include "client_internal.h"
#include <stddef.h>

/* --------------------------------------------------- compiler glue (libc) */

void *memcpy(void *d, const void *s, size_t n) {
  uint8_t *dst = d;
  const uint8_t *src = s;
  while (n--) *dst++ = *src++;
  return d;
}

void *memmove(void *d, const void *s, size_t n) {
  uint8_t *dst = d;
  const uint8_t *src = s;
  if (dst < src) {
    while (n--) *dst++ = *src++;
  } else {
    dst += n;
    src += n;
    while (n--) *--dst = *--src;
  }
  return d;
}

void *memset(void *d, int c, size_t n) {
  uint8_t *dst = d;
  while (n--) *dst++ = (uint8_t)c;
  return d;
}

size_t strlen(const char *s) {
  const char *p = s;
  while (*p) p++;
  return (size_t)(p - s);
}

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return (uint8_t)*a - (uint8_t)*b;
}

char *strcpy(char *d, const char *s) {
  char *r = d;
  while ((*d++ = *s++)) {}
  return r;
}

/* host imports (provided by runtime/browser/host.js) */
__attribute__((import_module("cerco"), import_name("dom_flush")))
extern void host_dom_flush(int32_t ptr, int32_t len);
__attribute__((import_module("cerco"), import_name("query")))
extern int32_t host_query(int32_t scope, int32_t sel_ptr, int32_t sel_len);
__attribute__((import_module("cerco"), import_name("value")))
extern int32_t host_value(int32_t node, int32_t out_ptr, int32_t cap);
__attribute__((import_module("cerco"), import_name("fetch")))
extern void host_fetch(int32_t id, int32_t method_ptr, int32_t method_len,
                       int32_t url_ptr, int32_t url_len, int32_t body_ptr,
                       int32_t body_len);
__attribute__((import_module("cerco"), import_name("nav_push")))
extern void host_nav_push(int32_t url_ptr, int32_t url_len);
__attribute__((import_module("cerco"), import_name("set_title")))
extern void host_set_title(int32_t ptr, int32_t len);
__attribute__((import_module("cerco"), import_name("log")))
extern void host_log(int32_t ptr, int32_t len);
__attribute__((import_module("cerco"), import_name("location")))
extern int32_t host_location(int32_t ptr, int32_t cap);

/* ------------------------------------------------------------------- heap */

extern uint8_t __heap_base; /* linker symbol */

#ifndef CERCO_MAX_HEAP_PAGES
#define CERCO_MAX_HEAP_PAGES 64
#endif

#define PAGE 65536u

static uintptr_t heap_ptr;
static uintptr_t heap_end; /* mapped memory end */

void cerco_heap_init(void) {
  heap_ptr = (uintptr_t)&__heap_base;
  heap_ptr = (heap_ptr + 15u) & ~(uintptr_t)15u;
  heap_end = __builtin_wasm_memory_size(0) * PAGE;
}

static int heap_grow(size_t need) {
  uintptr_t target = heap_ptr + need;
  if (target > CERCO_MAX_HEAP_PAGES * PAGE) return 0;
  uintptr_t pages_end = (heap_end + PAGE - 1) & ~(uintptr_t)(PAGE - 1);
  while (pages_end < target) {
    if (__builtin_wasm_memory_grow(0, 1) == (uint32_t)-1) return 0;
    pages_end += PAGE;
  }
  heap_end = pages_end;
  return 1;
}

void *cerco_alloc(size_t n) {
  n = (n + 15u) & ~(size_t)15u;
  if (!n) n = 16;
  if (heap_ptr + n > heap_end && !heap_grow(n)) return 0;
  void *p = (void *)heap_ptr;
  heap_ptr += n;
  return p;
}

char *cerco_strdup(const char *s) {
  size_t n = strlen(s) + 1;
  char *d = cerco_alloc(n);
  if (d) memcpy(d, s, n);
  return d;
}

/* ------------------------------------------------------------- scratch */

/* The host writes inbound payloads (hydration props, navigation paths) into
 * the scratch region via cerco_scratch(). It is also used for outbound
 * strings (urls, selectors). Reset per batch to keep memory flat. */
#define SCRATCH_SIZE 65536

static uint8_t *scratch_base = 0;
static size_t scratch_off;

void cerco_scratch_reset(void) { scratch_off = 0; }

void *cerco_scratch_alloc(size_t n) {
  if (!scratch_base) {
    scratch_base = cerco_alloc(SCRATCH_SIZE);
    if (!scratch_base) return 0;
  }
  n = (n + 7u) & ~(size_t)7u;
  if (scratch_off + n > SCRATCH_SIZE) return 0;
  void *p = scratch_base + scratch_off;
  scratch_off += n;
  return p;
}

char *cerco_scratch_strdup(const char *s) {
  size_t n = strlen(s) + 1;
  char *d = cerco_scratch_alloc(n);
  if (d) memcpy(d, s, n);
  return d;
}

/* stickiness: allocations that survive heap rewinds (navigation) */
static uintptr_t heap_sticky_floor;

void *cerco_alloc_sticky(size_t n) {
  uintptr_t before = heap_ptr;
  void *p = cerco_alloc(n);
  if (p) heap_sticky_floor = before + ((n + 15u) & ~(size_t)15u);
  return p;
}

void cerco_heap_rewind(void) {
  heap_ptr = heap_sticky_floor ? heap_sticky_floor : (uintptr_t)&__heap_base;
}

/* exported for the host: where it may write inbound data */
__attribute__((export_name("cerco_scratch")))
int32_t cerco_scratch(void) {
  if (!scratch_base) scratch_base = cerco_alloc_sticky(SCRATCH_SIZE);
  return (int32_t)scratch_base;
}

__attribute__((export_name("cerco_scratch_size")))
int32_t cerco_scratch_size(void) { return SCRATCH_SIZE; }

void cerco_debug_log(const char *s) { host_log((int32_t)s, (int32_t)strlen(s)); }

/* expose scratch internals to the runtime (not part of public API) */
uint8_t *cerco_scratch_base(void) {
  if (!scratch_base) scratch_base = cerco_alloc_sticky(SCRATCH_SIZE);
  return scratch_base;
}

void cerco_scratch_advance(size_t n) {
  n = (n + 7u) & ~(size_t)7u;
  if (scratch_off + n <= SCRATCH_SIZE) scratch_off += n;
}

void cerco_heap_init_public(void) { cerco_heap_init(); }

/* vsnprintf-free formatting helpers used by bindings */
int32_t cerco_i32_to_str(char *buf, int32_t v) {
  char tmp[12];
  int i = 0, o = 0;
  uint32_t u = v < 0 ? (uint32_t)-(int64_t)v : (uint32_t)v;
  if (u == 0) tmp[i++] = '0';
  while (u) { tmp[i++] = (char)('0' + u % 10); u /= 10; }
  if (v < 0) buf[o++] = '-';
  while (i) buf[o++] = tmp[--i];
  buf[o] = 0;
  return o;
}

int memcmp(const void *a, const void *b, size_t n) {
  const uint8_t *pa = a;
  const uint8_t *pb = b;
  for (size_t i = 0; i < n; i++) {
    if (pa[i] != pb[i]) return (int)pa[i] - (int)pb[i];
  }
  return 0;
}

char *strstr(const char *haystack, const char *needle) {
  if (!*needle) return (char *)haystack;
  for (; *haystack; haystack++) {
    const char *h = haystack;
    const char *n = needle;
    while (*h && *n && *h == *n) { h++; n++; }
    if (!*n) return (char *)haystack;
  }
  return 0;
}

int strncmp(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++) {
    unsigned char x = (unsigned char)a[i], y = (unsigned char)b[i];
    if (x != y) return (int)x - (int)y;
    if (!x) return 0;
  }
  return 0;
}

/* Minimal always-truncating formatter for client code: %s, %d/%i, %%.
 * The wasm client has no libc printf and never will (it would drag in the
 * whole float/locale machinery); these two verbs cover building URLs,
 * labels and selectors, which is what components actually do.
 * Returns the length written (never >= cap; buf is always NUL-terminated). */
int32_t cerco_format(char *buf, int32_t cap, const char *fmt, ...) {
  if (!buf || cap <= 0) return 0;
  int32_t o = 0;
  va_list ap;
  va_start(ap, fmt);
  for (const char *p = fmt; *p && o + 1 < cap; p++) {
    if (*p != '%') { buf[o++] = *p; continue; }
    p++;
    if (*p == 's') {
      const char *s = va_arg(ap, const char *);
      if (!s) s = "(null)";
      while (*s && o + 1 < cap) buf[o++] = *s++;
    } else if (*p == 'd' || *p == 'i') {
      char tmp[12];
      int32_t n = cerco_i32_to_str(tmp, va_arg(ap, int32_t));
      for (int32_t i = 0; i < n && o + 1 < cap; i++) buf[o++] = tmp[i];
    } else if (*p == '%') {
      buf[o++] = '%';
    } else if (!*p) {
      break; /* trailing '%' */
    } else {
      buf[o++] = '%'; /* unknown verb: emit it literally */
      if (o + 1 < cap) buf[o++] = *p;
    }
  }
  va_end(ap);
  buf[o] = 0;
  return o;
}
