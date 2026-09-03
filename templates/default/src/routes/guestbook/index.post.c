#include <cerco.h>
#include "server/guestbook.h"

/* answers POST /guestbook: validate, store, redirect (303) back to the GET.
 * Redirect-after-submit means a browser refresh never double-posts. */
CERCO_ROUTE {
  const char *name = cerco_form(r, "name");
  const char *message = cerco_form(r, "message");
  if (!name || !name[0] || !message || !message[0]) {
    cerco_redirect(r, "/guestbook?e=1");
    return;
  }
  guestbook_add(name, message);
  cerco_redirect(r, "/guestbook");
}
