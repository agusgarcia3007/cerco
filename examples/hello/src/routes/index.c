#include <cerco.h>

CERCO_ROUTE {
  cerco_raw(r, "<!DOCTYPE html>");
  cerco_tag(r, "html", NULL) {
    cerco_tag(r, "body", NULL) {
      cerco_tag(r, "h1", NULL) { cerco_raw(r, "hello, cerco"); }
    }
  }
}
