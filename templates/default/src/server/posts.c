#include "posts.h"
#include <string.h>

static const post posts[] = {
    {
        .slug = "one-language",
        .title = "One language on both ends",
        .date = "2026-08-30",
        .excerpt =
            "Why this site is two C compilers deep and zero JavaScript "
            "frameworks deep.",
        .paras = {
            "Every page on this site is written in C. The markup you are "
            "reading was rendered by a native server, and the interactive "
            "bits were compiled to WebAssembly by the same language and, "
            "almost, the same compiler.",
            "The framework owns about two hundred and fifty lines of "
            "JavaScript that load the wasm client and wire it to the DOM. "
            "That file never changes; it is part of cerco, not of this "
            "application, and it is small enough to read in one sitting.",
            "The point is not novelty. The point is that a one-person "
            "project can hold the whole stack in its head: one language, "
            "one build, one binary to deploy.",
            NULL,
        },
    },
    {
        .slug = "the-router",
        .title = "A file system is a router",
        .date = "2026-08-21",
        .excerpt =
            "src/routes/index.c serves /, [slug].c captures a parameter, "
            ".post.c answers POST. That is the whole convention.",
        .paras = {
            "There is no route configuration in this project. The build "
            "walks src/routes/ and turns each file into a URL: a directory "
            "becomes a path segment, index.c becomes the directory itself, "
            "and a bracketed name like [slug].c becomes a parameter.",
            "Method suffixes handle the rest. guestbook/index.c answers GET "
            "and guestbook/index.post.c answers POST for the same address. "
            "The build emits a static table of methods, paths and function "
            "pointers, and the server parses it once at startup.",
            "If you are reading this template, open src/routes/ in an "
            "editor and match each file against its address in the browser. "
            "The mapping is deliberately boring.",
            NULL,
        },
    },
    {
        .slug = "arenas",
        .title = "An arena per request",
        .date = "2026-08-14",
        .excerpt =
            "Handlers allocate as they like; the arena frees everything at "
            "once when the response is gone.",
        .paras = {
            "A web request is a short-lived problem. cerco gives each one "
            "an arena: a bump allocator over a bounded block of memory. "
            "Handlers call functions that allocate freely and never free, "
            "because nothing needs to be freed one object at a time.",
            "When the response has been written to the socket, the arena "
            "and everything in it disappear together. There is no way for "
            "this code to leak per-request memory and no way for it to "
            "free something still in use.",
            "The cap is enforced. A handler that tries to allocate beyond "
            "CERCO_REQUEST_MEMORY_LIMIT gets an allocation failure instead "
            "of a slow death by exhaustion.",
            NULL,
        },
    },
};

static const size_t n_posts = sizeof(posts) / sizeof(posts[0]);

const post *post_all(size_t *n) {
  *n = n_posts;
  return posts;
}

const post *post_find(const char *slug) {
  if (!slug) return NULL;
  for (size_t i = 0; i < n_posts; i++) {
    if (!strcmp(posts[i].slug, slug)) return &posts[i];
  }
  return NULL;
}
