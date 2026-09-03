/* guestbook store: fixed ring in server memory. A restart clears it — the
 * point of this module is to demonstrate a POST handler, validation and
 * redirect-after-submit, not persistence. */
#ifndef GUESTBOOK_H
#define GUESTBOOK_H

#include <stddef.h>

void guestbook_add(const char *name, const char *message);

typedef struct {
  const char *name;
  const char *message;
  const char *when; /* "2026-09-03 14:02" */
} guestbook_entry;

/* newest first; valid until the next guestbook_add */
const guestbook_entry *guestbook_entries(size_t *n);

#endif
