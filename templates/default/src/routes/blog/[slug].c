#include <cerco.h>
#include "server/posts.h"

CERCO_ROUTE {
  const post *p = post_find(cerco_param(r, "slug"));
  if (!p) {
    /* unknown slug: this page answers 404 itself, still inside the layout */
    cerco_status(r, 404);
    cerco_tag(r, "p", CERCO_CLASS("label")) { cerco_raw(r, "404 &middot; unknown slug"); }
    cerco_tag(r, "h1", CERCO_CLASS("mt-6 font-display text-3xl tracking-tight")) {
      cerco_textf(r, "No essay lives at &ldquo;%s&rdquo;.", cerco_param(r, "slug"));
    }
    cerco_tag(r, "p", CERCO_CLASS("mt-6 text-[15px]")) {
      cerco_tag(r, "a", CERCO_ATTRS(
          {"href", "/blog"}, {"class", "underline hover:text-rust"})) {
        cerco_raw(r, "&larr; back to the index");
      }
    }
    return;
  }

  cerco_tag(r, "time", CERCO_ATTRS({"datetime", p->date}, {"class", "label"})) {
    cerco_text(r, p->date);
  }
  cerco_tag(r, "h1", CERCO_CLASS("mt-4 font-display text-4xl leading-[1.15] tracking-tight")) {
    cerco_text(r, p->title);
  }
  cerco_tag(r, "div", CERCO_CLASS("mt-8 max-w-xl")) {
    for (int i = 0; p->paras[i]; i++) {
      cerco_tag(r, "p", CERCO_CLASS(i == 0 ? "dropcap mt-5 text-[17px] leading-[1.75]"
                                           : "mt-5 text-[17px] leading-[1.75]")) {
        cerco_text(r, p->paras[i]);
      }
    }
  }
  cerco_tag(r, "p", CERCO_CLASS("mt-12 text-sm")) {
    cerco_tag(r, "a", CERCO_ATTRS(
        {"href", "/blog"}, {"class", "underline hover:text-rust"})) {
      cerco_raw(r, "&larr; back to the index");
    }
  }
}
