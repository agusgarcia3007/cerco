#include <cerco.h>

/* served when nothing matches: src/routes/404.c -> the framework's
 * not-found handler picks this up automatically */
CERCO_ROUTE {
  cerco_title(r, "404 · my-app");

  cerco_tag(r, "div", CERCO_CLASS("mx-auto max-w-5xl px-6 py-24")) {
    cerco_tag(r, "p", CERCO_CLASS(
        "font-mono text-[11px] uppercase tracking-[0.18em] text-spark")) {
      cerco_raw(r, "nothing at this address");
    }
    cerco_tag(r, "h1", CERCO_CLASS(
        "mt-4 text-7xl font-semibold tracking-tight")) {
      cerco_raw(r, "404");
    }
    cerco_tag(r, "p", CERCO_CLASS("mt-6 max-w-lg leading-relaxed text-dim")) {
      cerco_raw(r, "The router walked its table and found no match. If you "
                   "expected this page to exist, add "
                   "<code class=\"font-mono text-xs text-ink\">"
                   "src/routes/&lt;name&gt;.c</code> and it will.");
    }
    cerco_tag(r, "p", CERCO_CLASS("mt-8 text-sm")) {
      cerco_tag(r, "a", CERCO_ATTRS(
          {"href", "/"}, {"class", "text-spark hover:underline"})) {
        cerco_raw(r, "&larr; back to the pokedex");
      }
    }
  }
}
