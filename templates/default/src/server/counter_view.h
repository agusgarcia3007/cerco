/* server-side markup for the two demo widgets (server-only).
 * The matching client code lives in src/components/. */
#include <cerco.h>

static void counter_demo(cerco_req *r) {
  cerco_tag(r, "div", CERCO_COMPONENT(r, "counter", "{\"start\":0}")) {
    cerco_tag(r, "div", CERCO_CLASS("mt-6 flex items-center gap-4")) {
      cerco_tag(r, "button", CERCO_ATTRS(
          {"data-cerco-b", "dec"},
          {"class", "h-10 w-10 rounded-lg border border-edge bg-panel text-xl "
                    "hover:border-faint transition-colors"})) {
        cerco_raw(r, "&minus;");
      }
      cerco_tag(r, "span", CERCO_ATTRS(
          {"data-cerco-b", "value"},
          {"class", "w-16 text-center font-mono text-xl"})) {
        cerco_raw(r, "0");
      }
      cerco_tag(r, "button", CERCO_ATTRS(
          {"data-cerco-b", "inc"},
          {"class", "h-10 w-10 rounded-lg bg-spark text-xl text-void "
                    "hover:opacity-90 transition-opacity"})) {
        cerco_raw(r, "+");
      }
    }
    cerco_tag(r, "p", CERCO_ATTRS(
        {"data-cerco-b", "msg"}, {"class", "mt-3 text-sm text-faint"})) {
      cerco_raw(r, "signals resume when the wasm client loads");
    }
  }
}

static void sf_demo(cerco_req *r) {
  cerco_tag(r, "div", CERCO_COMPONENT(r, "sumdemo", "{}")) {
    cerco_tag(r, "div", CERCO_CLASS("mt-6 flex items-center gap-4")) {
      cerco_tag(r, "button", CERCO_ATTRS(
          {"data-cerco-b", "go"},
          {"class", "rounded-lg border border-edge bg-panel px-5 py-2.5 "
                    "font-mono text-[11px] uppercase tracking-[0.18em] "
                    "hover:border-faint transition-colors"})) {
        cerco_raw(r, "add 2 + 3 on the server");
      }
      cerco_tag(r, "span", CERCO_ATTRS(
          {"data-cerco-b", "result"},
          {"class", "font-mono text-xl text-spark"})) {
        cerco_raw(r, "&nbsp;");
      }
    }
  }
}
