#include <cerco.h>

CERCO_ROUTE {
  cerco_set_header(r, "X-Test", "index");
  cerco_raw(r, "<h1>index</h1>");
}
