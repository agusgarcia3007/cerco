/* server-side markup for the counter (server-only) */
#include <cerco.h>

static void counter_demo(cerco_req *r) {
  cerco_tag(r, "div", CERCO_COMPONENT(r, "counter", "{\"start\":0}")) {
    cerco_tag(r, "div", CERCO_CLASS("flex items-center gap-3 my-6")) {
      cerco_tag(r, "button", CERCO_ATTRS({"data-cerco-b", "dec"}, {"class", "h-10 w-10 rounded-lg bg-slate-800 hover:bg-slate-700 text-xl"})) { cerco_raw(r, "&minus;"); }
      cerco_tag(r, "span", CERCO_ATTRS({"data-cerco-b", "value"}, {"class", "text-2xl font-mono w-16 text-center"})) { cerco_raw(r, "0"); }
      cerco_tag(r, "button", CERCO_ATTRS({"data-cerco-b", "inc"}, {"class", "h-10 w-10 rounded-lg bg-emerald-600 hover:bg-emerald-500 text-xl"})) { cerco_raw(r, "+"); }
    }
    cerco_tag(r, "p", CERCO_ATTRS({"data-cerco-b", "msg"}, {"class", "text-slate-500 text-sm"})) { cerco_raw(r, "signals are live once the wasm client resumes"); }
  }
}

static void sf_demo(cerco_req *r) {
  cerco_tag(r, "div", CERCO_COMPONENT(r, "sumdemo", "{}")) {
    cerco_tag(r, "div", CERCO_CLASS("flex items-center gap-3 my-6")) {
      cerco_tag(r, "button", CERCO_ATTRS({"data-cerco-b", "go"}, {"class", "px-4 h-10 rounded-lg bg-indigo-600 hover:bg-indigo-500"})) {
        cerco_raw(r, "call server function 2+3");
      }
      cerco_tag(r, "span", CERCO_ATTRS({"data-cerco-b", "result"}, {"class", "font-mono text-indigo-300"})) { cerco_raw(r, " "); }
    }
  }
}
