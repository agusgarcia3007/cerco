#include <cerco.h>

CERCO_ROUTE {
  const char *name = cerco_query_get(r, "name");
  cerco_raw(r, "hello ");
  cerco_text(r, name ? name : "world");
}
