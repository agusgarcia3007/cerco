#include <cerco.h>
#include <stdio.h>
#include "server/posts.h"

CERCO_ROUTE {
  size_t n = 0;
  const post *all = post_all(&n);

  cerco_tag(r, "p", CERCO_CLASS("label")) { cerco_raw(r, "The blog"); }
  cerco_tag(r, "h1", CERCO_CLASS("mt-6 font-display text-4xl tracking-tight")) {
    cerco_raw(r, "Essays");
  }

  cerco_tag(r, "div", CERCO_CLASS("mt-10 border-t border-rule")) {
    for (size_t i = 0; i < n; i++) {
      const post *p = &all[i];
      char href[128];
      snprintf(href, sizeof(href), "/blog/%s", p->slug);
      cerco_tag(r, "article", CERCO_CLASS("border-b border-rule py-6")) {
        cerco_tag(r, "a", CERCO_ATTRS(
            {"href", href},
            {"class", "group block"})) {
          cerco_tag(r, "time", CERCO_ATTRS(
              {"datetime", p->date}, {"class", "label"})) {
            cerco_text(r, p->date);
          }
          cerco_tag(r, "h2", CERCO_CLASS(
              "mt-2 font-display text-2xl tracking-tight group-hover:text-rust")) {
            cerco_text(r, p->title);
          }
          cerco_tag(r, "p", CERCO_CLASS("mt-2 text-[15px] leading-relaxed text-faded max-w-xl")) {
            cerco_text(r, p->excerpt);
          }
        }
      }
    }
  }
}
