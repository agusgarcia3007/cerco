#include <cerco.h>

/* served when nothing matches: src/routes/404.c -> the framework's
 * not-found handler picks this up automatically */
CERCO_ROUTE {
  cerco_tag(r, "p", CERCO_CLASS("label")) { cerco_raw(r, "Nothing at this address"); }
  cerco_tag(r, "h1", CERCO_CLASS("mt-6 font-display text-7xl tracking-tight text-rust")) {
    cerco_raw(r, "404");
  }
  cerco_tag(r, "p", CERCO_CLASS("mt-6 text-[15px] leading-relaxed max-w-xl")) {
    cerco_raw(r, "The router walked its table and found no match. If you expected "
                 "this page to exist, add <code class=\"font-mono text-xs\">src/routes/&lt;name&gt;.c</code> "
                 "and it will.");
  }
  cerco_tag(r, "p", CERCO_CLASS("mt-8 text-sm")) {
    cerco_tag(r, "a", CERCO_ATTRS(
        {"href", "/"}, {"class", "underline hover:text-rust"})) {
      cerco_raw(r, "&larr; back to the front page");
    }
  }
}
