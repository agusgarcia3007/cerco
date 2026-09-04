/* server-side markup for the pokedex widget (server-only).
 * The matching client code lives in src/components/pokedex.c. */
#include <cerco.h>

static void pokedex_grid(cerco_req *r) {
  cerco_tag(r, "div", CERCO_CLASS("mx-auto max-w-5xl px-6 pb-20")) {
    cerco_tag(r, "div", CERCO_COMPONENT(r, "pokedex", "{}")) {
      cerco_tag(r, "div", CERCO_CLASS("flex items-center gap-4")) {
        cerco_void_tag(r, "input", CERCO_ATTRS(
            {"data-cerco-b", "filter"},
            {"type", "search"},
            {"placeholder", "Filter by name…"},
            {"autocomplete", "off"},
            {"class", "w-full max-w-xs rounded-lg border border-edge bg-panel "
                      "px-3 py-2 text-sm placeholder:text-faint "
                      "focus:outline-none focus:border-spark"}));
        cerco_tag(r, "span", CERCO_ATTRS(
            {"data-cerco-b", "count"},
            {"class", "font-mono text-xs text-faint"})) { cerco_raw(r, ""); }
      }
      cerco_tag(r, "p", CERCO_ATTRS(
          {"data-cerco-b", "status"},
          {"class", "mt-8 text-sm text-dim"})) {
        cerco_raw(r, "fetching pokeapi.co from wasm&hellip;");
      }
      cerco_tag(r, "div", CERCO_ATTRS(
          {"data-cerco-b", "grid"},
          {"class", "mt-6 grid grid-cols-2 sm:grid-cols-3 md:grid-cols-4 gap-3"})) {
      }
    }
  }
}
