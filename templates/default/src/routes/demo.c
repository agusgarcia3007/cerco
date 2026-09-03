#include <cerco.h>
#include "server/counter_view.h"

CERCO_ROUTE {
  cerco_tag(r, "p", CERCO_CLASS("label")) { cerco_raw(r, "Demo"); }
  cerco_tag(r, "h1", CERCO_CLASS("mt-6 font-display text-4xl tracking-tight")) {
    cerco_raw(r, "The interactive bits.");
  }

  cerco_tag(r, "section", CERCO_CLASS("mt-10 border-t border-rule pt-8")) {
    cerco_tag(r, "h2", CERCO_CLASS("font-display text-2xl tracking-tight")) {
      cerco_raw(r, "Signals, hydrated from wasm");
    }
    cerco_tag(r, "p", CERCO_CLASS("mt-3 text-[15px] leading-relaxed text-faded max-w-xl")) {
      cerco_raw(r, "The counter is a client component: C from "
                   "<code class=\"font-mono text-xs\">src/components/counter.c</code>, compiled to "
                   "WebAssembly, resumed in your browser. State is a signal bound to the "
                   "text node &mdash; no virtual DOM, no diffing.");
    }
    counter_demo(r);
  }

  cerco_tag(r, "section", CERCO_CLASS("mt-12 border-t border-rule pt-8")) {
    cerco_tag(r, "h2", CERCO_CLASS("font-display text-2xl tracking-tight")) {
      cerco_raw(r, "A function call to the server");
    }
    cerco_tag(r, "p", CERCO_CLASS("mt-3 text-[15px] leading-relaxed text-faded max-w-xl")) {
      cerco_raw(r, "The button calls <code class=\"font-mono text-xs\">cerco_sf_add(2, 3, on_result)</code> "
                   "from the client and the answer comes back from "
                   "<code class=\"font-mono text-xs\">src/server/functions.c</code> over a small binary "
                   "protocol. Server functions are declared once in "
                   "<code class=\"font-mono text-xs\">src/server/functions.x</code>.");
    }
    sf_demo(r);
  }
}
