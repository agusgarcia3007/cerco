#include <cerco.h>

/* custom 404 page: src/routes/404.c */
CERCO_ROUTE {
  cerco_tag(r, "h1", CERCO_CLASS("text-3xl font-bold mb-4")) {
    cerco_raw(r, "404");
  }
  cerco_tag(r, "p", CERCO_CLASS("text-slate-400")) {
    cerco_raw(r, "that page does not exist. ");
  }
  cerco_tag(r, "a", CERCO_ATTRS({"href", "/"}, {"class", "text-emerald-400 hover:underline"})) {
    cerco_raw(r, "go home");
  }
}
