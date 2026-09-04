#include <cerco.h>
#include "server/counter_view.h"

CERCO_ROUTE {
  cerco_title(r, "Demo · my-app");

  cerco_tag(r, "div", CERCO_CLASS("mx-auto max-w-3xl px-6 py-16")) {
    cerco_tag(r, "p", CERCO_CLASS(
        "font-mono text-[11px] uppercase tracking-[0.18em] text-faint")) {
      cerco_raw(r, "demo");
    }
    cerco_tag(r, "h1", CERCO_CLASS(
        "mt-4 text-4xl font-semibold tracking-tight")) {
      cerco_raw(r, "The interactive bits.");
    }

    cerco_tag(r, "section", CERCO_CLASS("mt-12 border-t border-edge pt-10")) {
      cerco_tag(r, "h2", CERCO_CLASS("text-xl font-semibold tracking-tight")) {
        cerco_raw(r, "Signals, hydrated from wasm");
      }
      cerco_tag(r, "p", CERCO_CLASS("mt-3 max-w-xl leading-relaxed text-dim")) {
        cerco_raw(r, "The counter is a client component: C from "
                     "<code class=\"font-mono text-xs text-ink\">"
                     "src/components/counter.c</code>, compiled to WebAssembly "
                     "and resumed in your browser. State is a signal bound to "
                     "the text node &mdash; no virtual DOM, no diffing.");
      }
      counter_demo(r);
    }

    cerco_tag(r, "section", CERCO_CLASS("mt-14 border-t border-edge pt-10")) {
      cerco_tag(r, "h2", CERCO_CLASS("text-xl font-semibold tracking-tight")) {
        cerco_raw(r, "A function call to the server");
      }
      cerco_tag(r, "p", CERCO_CLASS("mt-3 max-w-xl leading-relaxed text-dim")) {
        cerco_raw(r, "The button calls "
                     "<code class=\"font-mono text-xs text-ink\">"
                     "cerco_sf_add(2, 3, on_result)</code> from the client and "
                     "the answer comes back from "
                     "<code class=\"font-mono text-xs text-ink\">"
                     "src/server/functions.c</code> over a small binary "
                     "protocol. Server functions are declared once in "
                     "<code class=\"font-mono text-xs text-ink\">"
                     "src/server/functions.x</code>.");
      }
      sf_demo(r);
    }
  }
}
