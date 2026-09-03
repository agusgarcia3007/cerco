#include <cerco.h>

CERCO_LAYOUT {
  cerco_raw(r, "<!DOCTYPE html>\n");
  cerco_tag(r, "html", CERCO_ATTRS({"lang", "en"})) {
    cerco_tag(r, "head", NULL) {
      cerco_void_tag(r, "meta", CERCO_ATTRS({"charset", "utf-8"}));
      cerco_void_tag(r, "meta", CERCO_ATTRS(
          {"name", "viewport"},
          {"content", "width=device-width, initial-scale=1"}));
      cerco_tag(r, "title", NULL) { cerco_raw(r, "my-app"); }
      cerco_void_tag(r, "meta", CERCO_ATTRS(
          {"name", "description"},
          {"content", "my-app — a small site built with cerco"}));
      cerco_void_tag(r, "link", CERCO_ATTRS({"rel", "icon"}, {"href", "/favicon.svg"}));
      cerco_void_tag(r, "link", CERCO_ATTRS(
          {"rel", "stylesheet"}, {"href", "/assets/styles.css"}));
      cerco_raw(r, "<script src=\"/assets/host.js\" defer></script>\n");
    }
    cerco_tag(r, "body", NULL) {
      cerco_tag(r, "div", CERCO_CLASS("mx-auto max-w-2xl px-6 min-h-screen flex flex-col")) {
        cerco_tag(r, "header",
                  CERCO_CLASS("flex items-baseline justify-between py-5 border-b border-rule")) {
          cerco_tag(r, "a", CERCO_ATTRS(
              {"href", "/"},
              {"class", "font-display italic text-lg hover:text-rust"})) {
            cerco_raw(r, "my-app");
          }
          cerco_tag(r, "nav", CERCO_CLASS("flex gap-5 font-mono text-[11px] uppercase tracking-[0.18em]")) {
            cerco_tag(r, "a", CERCO_ATTRS({"href", "/blog"}, {"class", "hover:text-rust"})) {
              cerco_raw(r, "blog");
            }
            cerco_tag(r, "a", CERCO_ATTRS({"href", "/guestbook"}, {"class", "hover:text-rust"})) {
              cerco_raw(r, "guestbook");
            }
            cerco_tag(r, "a", CERCO_ATTRS({"href", "/demo"}, {"class", "hover:text-rust"})) {
              cerco_raw(r, "demo");
            }
          }
        }
        cerco_tag(r, "main", CERCO_CLASS("rise py-12 grow")) {
          cerco_layout_children(r);
        }
        cerco_tag(r, "footer",
                  CERCO_CLASS("flex items-baseline justify-between py-5 border-t border-rule")) {
          cerco_tag(r, "span", CERCO_CLASS("label")) { cerco_raw(r, "my-app"); }
          cerco_tag(r, "span", CERCO_CLASS("label")) {
            cerco_raw(r, "built with <a href=\"https://github.com/agusgarcia3007/cerco\" class=\"underline hover:text-rust\">cerco</a>");
          }
        }
      }
    }
  }
}
