#include <cerco.h>

CERCO_ROUTE_POST {
  size_t len = 0;
  const char *body = cerco_body(r, &len);
  const char *v = cerco_form(r, "field");
  if (v) { cerco_raw(r, "field="); cerco_raw(r, v); }
  else if (body) cerco_write_bytes(r, body, len);
  else cerco_raw(r, "(no body)");
}
