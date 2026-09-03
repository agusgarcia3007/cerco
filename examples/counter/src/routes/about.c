#include <cerco.h>

CERCO_ROUTE {
  cerco_tag(r, "h1", CERCO_CLASS("text-3xl font-bold mb-4")) {
    cerco_raw(r, "About");
  }
  cerco_tag(r, "p", CERCO_CLASS("text-slate-400 mb-2")) {
    cerco_raw(r, "One language. One native server. One tiny wasm client.");
  }
  cerco_tag(r, "ul", CERCO_CLASS("list-disc list-inside text-slate-400 mt-4 space-y-1")) {
    cerco_tag(r, "li", NULL) { cerco_raw(r, "server: C compiled to a native binary (libuv + llhttp)"); }
    cerco_tag(r, "li", NULL) { cerco_raw(r, "client: the same language compiled to WebAssembly"); }
    cerco_tag(r, "li", NULL) { cerco_raw(r, "host: a few hundred lines of framework-owned JavaScript"); }
  }
}
