#include <cerco.h>
#include <stdio.h>
#include <string.h>
#include "server/pokemon_view.h"

/* /pokemon/<name> — one file, every name.
 *
 * The name is interpolated into the component props (JSON), so it is
 * validated here rather than trusted: anything that is not a plain slug
 * answers 404 and never reaches the client. */
static int valid_slug(const char *s) {
  if (!s || !*s) return 0;
  size_t n = strlen(s);
  if (n > 24) return 0;
  for (size_t i = 0; i < n; i++) {
    char c = s[i];
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return 0;
  }
  return 1;
}

CERCO_ROUTE {
  const char *name = cerco_param(r, "name");

  if (!valid_slug(name)) {
    cerco_status(r, 404);
    pokemon_not_found(r, name);
    return;
  }

  char title[64], props[64];
  snprintf(title, sizeof(title), "%s · my-app", name);
  cerco_title(r, title); /* per-page <title>, patched into the layout's head */

  snprintf(props, sizeof(props), "{\"name\":\"%s\"}", name);
  pokemon_detail(r, name, props);
}
