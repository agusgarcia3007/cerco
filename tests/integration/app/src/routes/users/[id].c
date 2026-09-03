#include <cerco.h>

CERCO_ROUTE {
  cerco_raw(r, "user=");
  cerco_raw(r, cerco_param(r, "id"));
}
