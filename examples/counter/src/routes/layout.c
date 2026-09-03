#include <cerco.h>

CERCO_LAYOUT {
  cerco_raw(r, "<!DOCTYPE html>\n");
  cerco_tag(r, "html", NULL) {
    cerco_tag(r, "head", NULL) {
      cerco_void_tag(r, "meta", CERCO_ATTRS({"charset", "utf-8"}));
      cerco_void_tag(r, "meta", CERCO_ATTRS(
          {"name", "viewport"},
          {"content", "width=device-width, initial-scale=1"}));
      cerco_tag(r, "title", NULL) { cerco_raw(r, "my-app"); }
      cerco_void_tag(r, "link", CERCO_ATTRS({"rel", "icon"}, {"href", "/favicon.svg"}));
      cerco_void_tag(r, "link", CERCO_ATTRS(
          {"rel", "stylesheet"}, {"href", "/assets/styles.css"}));
      cerco_raw(r, "<script src=\"/assets/host.js\" defer></script>\n");
    }
    cerco_tag(r, "body", CERCO_CLASS("min-h-screen bg-slate-950 text-slate-100 antialiased")) {
      cerco_tag(r, "nav", CERCO_CLASS("flex gap-6 p-6 border-b border-slate-800")) {
        cerco_tag(r, "a", CERCO_ATTRS({"href", "/"}, {"class", "font-bold text-emerald-400"})) {
          cerco_raw(r, "cerco");
        }
        cerco_tag(r, "a", CERCO_ATTRS({"href", "/about"}, {"class", "hover:text-emerald-300"})) {
          cerco_raw(r, "about");
        }
      }
      cerco_tag(r, "main", CERCO_CLASS("max-w-3xl mx-auto p-6")) {
        cerco_layout_children(r);
      }
      cerco_tag(r, "footer", CERCO_CLASS("p-6 text-xs text-slate-600 border-t border-slate-900")) {
        cerco_raw(r, "served by cerco &mdash; one language, one native server");
      }
    }
  }
}
