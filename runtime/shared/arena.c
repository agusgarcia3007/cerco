#include "arena.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define CERCO_ARENA_CHUNK_MAX (4u * 1024u * 1024u)

static cerco_achunk *chunk_new(size_t cap) {
  cerco_achunk *c = (cerco_achunk *)malloc(sizeof(cerco_achunk) + cap);
  if (!c) return NULL;
  c->next = NULL;
  c->cap = cap;
  c->used = 0;
  return c;
}

static void arena_oom_default(void) { abort(); }
static void (*arena_oom_fn)(void) = arena_oom_default;

void cerco_arena_set_oom_handler(void (*fn)(void)) { arena_oom_fn = fn ? fn : arena_oom_default; }

void cerco_arena_init(cerco_arena *a, size_t first_chunk, size_t hard_cap) {
  memset(a, 0, sizeof(*a));
  a->hard_cap = hard_cap;
  if (first_chunk < 64) first_chunk = 64;
  a->head = chunk_new(first_chunk);
  a->cur = a->head;
  if (a->head) a->total = first_chunk;
  else a->oom = 1;
}

static int arena_grow(cerco_arena *a, size_t need) {
  if (a->hard_cap && a->total + need > a->hard_cap) {
    a->oom = 1;
    return 0;
  }
  size_t cap = a->cur ? a->cur->cap * 2 : 8192;
  if (cap < need) cap = need;
  if (cap > CERCO_ARENA_CHUNK_MAX) cap = CERCO_ARENA_CHUNK_MAX;
  cerco_achunk *c = chunk_new(cap);
  if (!c) { a->oom = 1; return 0; }
  a->cur->next = c;
  a->cur = c;
  a->total += cap;
  return 1;
}

void *cerco_arena_alloc_aligned(cerco_arena *a, size_t size, size_t align) {
  if (size == 0) size = 1;
  if (!a->cur && !arena_grow(a, size)) { arena_oom_fn(); return NULL; }
  for (;;) {
    uintptr_t hdr_end = (uintptr_t)a->cur + sizeof(cerco_achunk);
    uintptr_t base = hdr_end + a->cur->used;
    uintptr_t aligned = (base + (align - 1)) & ~(uintptr_t)(align - 1);
    size_t pad = (size_t)(aligned - base);
    if (pad + size <= a->cur->cap - a->cur->used) {
      a->cur->used += pad + size;
      return (void *)aligned;
    }
    if (!arena_grow(a, size)) { arena_oom_fn(); return NULL; }
  }
}

void *cerco_arena_alloc(cerco_arena *a, size_t size) { return cerco_arena_alloc_aligned(a, size, 8); }

void *cerco_arena_alloc0(cerco_arena *a, size_t size) {
  void *p = cerco_arena_alloc(a, size);
  if (p) memset(p, 0, size);
  return p;
}

void *cerco_arena_memdup(cerco_arena *a, const void *src, size_t len) {
  void *p = cerco_arena_alloc(a, len);
  if (p && src) memcpy(p, src, len);
  return p;
}

char *cerco_arena_strndup(cerco_arena *a, const char *s, size_t len) {
  char *p = (char *)cerco_arena_alloc(a, len + 1);
  if (p) { memcpy(p, s, len); p[len] = 0; }
  return p;
}

char *cerco_arena_strdup(cerco_arena *a, const char *s) {
  return cerco_arena_strndup(a, s, strlen(s));
}

char *cerco_arena_sprintf(cerco_arena *a, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  va_list ap2;
  va_copy(ap2, ap);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n < 0) { va_end(ap2); return NULL; }
  char *p = (char *)cerco_arena_alloc(a, (size_t)n + 1);
  if (p) vsnprintf(p, (size_t)n + 1, fmt, ap2);
  va_end(ap2);
  return p;
}

void cerco_arena_reset(cerco_arena *a) {
  cerco_achunk *c = a->head ? a->head->next : NULL;
  while (c) {
    cerco_achunk *next = c->next;
    free(c);
    c = next;
  }
  if (a->head) { a->head->next = NULL; a->head->used = 0; }
  a->cur = a->head;
  a->total = a->head ? a->head->cap : 0;
  a->oom = 0;
}

void cerco_arena_destroy(cerco_arena *a) {
  cerco_achunk *c = a->head;
  while (c) {
    cerco_achunk *next = c->next;
    free(c);
    c = next;
  }
  memset(a, 0, sizeof(*a));
}
