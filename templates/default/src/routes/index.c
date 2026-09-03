#include <cerco.h>

CERCO_ROUTE {
  cerco_tag(r, "p", CERCO_CLASS("label")) { cerco_raw(r, "my-app &middot; a cerco starter"); }
  cerco_tag(r, "h1", CERCO_CLASS("mt-6 font-display text-4xl leading-[1.12] tracking-tight")) {
    cerco_raw(r, "A small site, compiled to one native binary.");
  }
  cerco_tag(r, "p", CERCO_CLASS("mt-6 text-[17px] leading-relaxed max-w-xl")) {
    cerco_raw(r, "This template is a working site, not a stub: file-based routes, path "
                 "parameters, a POST form with redirect-after-submit, and two interactive "
                 "components hydrated from a wasm client. Every page maps to one file "
                 "under <code class=\"font-mono text-sm\">src/routes/</code> &mdash; open it and compare.");
  }

  cerco_tag(r, "div", CERCO_CLASS("mt-12")) {
    cerco_tag(r, "h2", CERCO_CLASS("label")) { cerco_raw(r, "Pages in this template"); }
    cerco_tag(r, "ul", CERCO_CLASS("mt-4 border-y border-rule divide-y divide-rule")) {
      cerco_tag(r, "li", NULL) {
        cerco_tag(r, "a", CERCO_ATTRS(
            {"href", "/blog"},
            {"class", "group flex items-baseline gap-5 py-4 hover:text-rust"})) {
          cerco_tag(r, "span", CERCO_CLASS("font-mono text-xs text-rust w-24 shrink-0")) {
            cerco_raw(r, "/blog");
          }
          cerco_tag(r, "span", CERCO_CLASS("text-[15px]")) {
            cerco_raw(r, "Three short essays, served from <em>src/server/posts.c</em> through a <em>[slug].c</em> route.");
          }
          cerco_tag(r, "span", CERCO_CLASS("ml-auto font-mono text-xs text-faded group-hover:text-rust")) {
            cerco_raw(r, "&rarr;");
          }
        }
      }
      cerco_tag(r, "li", NULL) {
        cerco_tag(r, "a", CERCO_ATTRS(
            {"href", "/guestbook"},
            {"class", "group flex items-baseline gap-5 py-4 hover:text-rust"})) {
          cerco_tag(r, "span", CERCO_CLASS("font-mono text-xs text-rust w-24 shrink-0")) {
            cerco_raw(r, "/guestbook");
          }
          cerco_tag(r, "span", CERCO_CLASS("text-[15px]")) {
            cerco_raw(r, "A form: GET renders it, <em>index.post.c</em> validates, stores and redirects back.");
          }
          cerco_tag(r, "span", CERCO_CLASS("ml-auto font-mono text-xs text-faded group-hover:text-rust")) {
            cerco_raw(r, "&rarr;");
          }
        }
      }
      cerco_tag(r, "li", NULL) {
        cerco_tag(r, "a", CERCO_ATTRS(
            {"href", "/demo"},
            {"class", "group flex items-baseline gap-5 py-4 hover:text-rust"})) {
          cerco_tag(r, "span", CERCO_CLASS("font-mono text-xs text-rust w-24 shrink-0")) {
            cerco_raw(r, "/demo");
          }
          cerco_tag(r, "span", CERCO_CLASS("text-[15px]")) {
            cerco_raw(r, "Client signals in wasm and a server function called over the wire.");
          }
          cerco_tag(r, "span", CERCO_CLASS("ml-auto font-mono text-xs text-faded group-hover:text-rust")) {
            cerco_raw(r, "&rarr;");
          }
        }
      }
    }
  }

  cerco_tag(r, "p", CERCO_CLASS("mt-10 text-sm text-faded max-w-xl leading-relaxed")) {
    cerco_raw(r, "While <code class=\"font-mono text-xs\">cerco dev</code> is running, edit any file "
                 "under <code class=\"font-mono text-xs\">src/</code> and reload &mdash; the page rebuilds "
                 "and the browser refreshes itself.");
  }
}
