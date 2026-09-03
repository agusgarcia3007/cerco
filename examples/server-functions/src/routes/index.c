#include <cerco.h>
#include "../server/counter_view.h"

CERCO_ROUTE {
  cerco_tag(r, "h1", CERCO_CLASS("text-3xl font-bold mb-4")) {
    cerco_raw(r, "It works.");
  }
  cerco_tag(r, "p", CERCO_CLASS("text-slate-400 mb-6")) {
    cerco_textf(r, "Hello from %s, served by native C.", "SSR");
  }
  counter_demo(r);
  sf_demo(r);
}
