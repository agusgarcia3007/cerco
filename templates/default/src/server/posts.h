/* server-only blog data. lives in src/server/ so it never reaches the wasm
 * client; routes render it, the client just receives markup. */
#ifndef POSTS_H
#define POSTS_H

#include <stddef.h>

typedef struct {
  const char *slug;
  const char *title;
  const char *date;   /* ISO, shown as-is */
  const char *excerpt;
  const char *paras[4]; /* NULL-terminated */
} post;

/* all posts, newest first */
const post *post_all(size_t *n);

/* lookup by slug; NULL when unknown */
const post *post_find(const char *slug);

#endif
