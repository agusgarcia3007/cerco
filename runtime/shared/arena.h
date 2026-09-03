/* cerco — arena allocator.
 *
 * Chunk-based arena with a hard cap. Memory is bump-allocated from chunks;
 * chunks are linked and freed together on destroy. reset() keeps the first
 * chunk (and its capacity) for reuse. No per-object free; ownership is the
 * arena itself. Never returns NULL: on allocation failure (including cap
 * exceeded) cerco_arena_oom() is invoked (aborts by default; request layer
 * installs a handler that marks the request as failed).
 */
#ifndef CERCO_ARENA_H
#define CERCO_ARENA_H

#include <stddef.h>
#include <stdint.h>

typedef struct cerco_achunk {
  struct cerco_achunk *next;
  size_t cap;
  size_t used;
} cerco_achunk;

typedef struct cerco_arena {
  cerco_achunk *head;
  cerco_achunk *cur;
  size_t total;      /* bytes allocated across chunks */
  size_t hard_cap;   /* 0 = unlimited */
  int oom;           /* set when an allocation hit the cap */
} cerco_arena;

void cerco_arena_init(cerco_arena *a, size_t first_chunk, size_t hard_cap);
void *cerco_arena_alloc(cerco_arena *a, size_t size);
void *cerco_arena_alloc0(cerco_arena *a, size_t size);
void *cerco_arena_alloc_aligned(cerco_arena *a, size_t size, size_t align);
/* duplicate a range; returns NULL only on OOM */
void *cerco_arena_memdup(cerco_arena *a, const void *src, size_t len);
char *cerco_arena_strndup(cerco_arena *a, const char *s, size_t len);
char *cerco_arena_strdup(cerco_arena *a, const char *s);
/* formatted alloc (printf style) */
char *cerco_arena_sprintf(cerco_arena *a, const char *fmt, ...);
/* free everything except the first chunk; keeps capacity for reuse */
void cerco_arena_reset(cerco_arena *a);
void cerco_arena_destroy(cerco_arena *a);
/* install a custom out-of-memory policy (NULL restores abort-on-oom) */
void cerco_arena_set_oom_handler(void (*fn)(void));
/* true if any allocation failed since init/reset */
static inline int cerco_arena_failed(const cerco_arena *a) { return a->oom; }

#endif
