#include <cerco.h>

/* the shell every page shares: header + nav above, footer below.
 * The header lives OUTSIDE the <!--cerco:page--> markers, so client-side
 * navigation swaps only the page region and the header never flickers. */
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
    cerco_tag(r, "body", CERCO_CLASS("min-h-screen flex flex-col antialiased")) {
      cerco_tag(r, "header", CERCO_CLASS(
          "sticky top-0 z-10 border-b border-edge bg-void/80 backdrop-blur")) {
        cerco_tag(r, "div", CERCO_CLASS(
            "mx-auto max-w-5xl px-6 h-14 flex items-center justify-between")) {
          cerco_tag(r, "a", CERCO_ATTRS(
              {"href", "/"},
              {"class", "flex items-center gap-2.5 font-semibold tracking-tight"})) {
            cerco_raw(r, "<svg width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" "
                         "fill=\"none\" aria-hidden=\"true\">"
                         "<rect width=\"24\" height=\"24\" rx=\"6\" fill=\"#ff5c1f\"/>"
                         "<text x=\"12\" y=\"16.5\" text-anchor=\"middle\" "
                         "font-family=\"ui-monospace, monospace\" font-size=\"13\" "
                         "font-weight=\"bold\" fill=\"#0a0a0a\">c</text></svg>");
            cerco_raw(r, "my-app");
          }
          cerco_tag(r, "nav", CERCO_CLASS(
              "flex items-center gap-6 font-mono text-[11px] uppercase tracking-[0.18em] text-dim")) {
            cerco_tag(r, "a", CERCO_ATTRS(
                {"href", "/"}, {"class", "hover:text-ink transition-colors"})) {
              cerco_raw(r, "pokedex");
            }
            cerco_tag(r, "a", CERCO_ATTRS(
                {"href", "/demo"}, {"class", "hover:text-ink transition-colors"})) {
              cerco_raw(r, "demo");
            }
            cerco_tag(r, "a", CERCO_ATTRS(
                {"href", "/guestbook"}, {"class", "hover:text-ink transition-colors"})) {
              cerco_raw(r, "guestbook");
            }
            cerco_tag(r, "a", CERCO_ATTRS(
                {"href", "https://github.com/agusgarcia3007/cerco"},
                {"class", "hover:text-ink transition-colors"})) {
              cerco_raw(r, "github");
            }
          }
        }
      }
      cerco_tag(r, "main", CERCO_CLASS("rise grow")) {
        cerco_layout_children(r);
      }
      cerco_tag(r, "footer", CERCO_CLASS("border-t border-edge")) {
        cerco_tag(r, "div", CERCO_CLASS(
            "mx-auto max-w-5xl px-6 py-6 flex items-center justify-between "
            "font-mono text-[11px] uppercase tracking-[0.18em] text-faint")) {
          cerco_tag(r, "span", NULL) { cerco_raw(r, "my-app"); }
          cerco_tag(r, "span", NULL) {
            cerco_raw(r, "built with <a href=\"https://github.com/agusgarcia3007/cerco\" "
                         "class=\"underline hover:text-spark\">cerco</a>");
          }
        }
      }
    }
  }
}
