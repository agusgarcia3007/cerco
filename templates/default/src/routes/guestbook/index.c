#include <cerco.h>
#include "server/guestbook.h"

CERCO_ROUTE {
  cerco_title(r, "Guestbook · my-app");

  size_t n = 0;
  const guestbook_entry *entries = guestbook_entries(&n);

  cerco_tag(r, "div", CERCO_CLASS("mx-auto max-w-3xl px-6 py-16")) {
    cerco_tag(r, "p", CERCO_CLASS(
        "font-mono text-[11px] uppercase tracking-[0.18em] text-faint")) {
      cerco_raw(r, "guestbook");
    }
    cerco_tag(r, "h1", CERCO_CLASS("mt-4 text-4xl font-semibold tracking-tight")) {
      cerco_raw(r, "Say something.");
    }
    cerco_tag(r, "p", CERCO_CLASS("mt-4 max-w-xl leading-relaxed text-dim")) {
      cerco_raw(r, "Entries live in server memory only &mdash; a restart clears "
                   "them. The point of this page is the flow: a GET renders the "
                   "form, <code class=\"font-mono text-xs text-ink\">"
                   "index.post.c</code> validates and stores, then redirects "
                   "back so a refresh never double-posts.");
    }

    if (cerco_query_get(r, "e")) {
      cerco_tag(r, "p", CERCO_CLASS(
          "mt-6 rounded-lg border border-spark bg-panel px-4 py-3 "
          "font-mono text-sm text-spark")) {
        cerco_raw(r, "both fields are required &mdash; try again");
      }
    }

    cerco_tag(r, "form", CERCO_ATTRS(
        {"method", "post"}, {"action", "/guestbook"},
        {"class", "mt-10 rounded-xl border border-edge bg-panel p-6"})) {
      cerco_tag(r, "label", CERCO_ATTRS(
          {"for", "name"},
          {"class", "block font-mono text-[11px] uppercase "
                    "tracking-[0.18em] text-faint"})) {
        cerco_raw(r, "name");
      }
      cerco_void_tag(r, "input", CERCO_ATTRS(
          {"id", "name"}, {"name", "name"}, {"required", "required"},
          {"maxlength", "40"}, {"autocomplete", "off"},
          {"class", "mt-2 w-full rounded-lg border border-edge bg-void px-3 "
                    "py-2 text-sm placeholder:text-faint focus:outline-none "
                    "focus:border-spark"}));
      cerco_tag(r, "label", CERCO_ATTRS(
          {"for", "message"},
          {"class", "mt-5 block font-mono text-[11px] uppercase "
                    "tracking-[0.18em] text-faint"})) {
        cerco_raw(r, "message");
      }
      cerco_tag(r, "textarea", CERCO_ATTRS(
          {"id", "message"}, {"name", "message"}, {"rows", "3"},
          {"required", "required"}, {"maxlength", "280"},
          {"class", "mt-2 w-full rounded-lg border border-edge bg-void px-3 "
                    "py-2 text-sm leading-relaxed placeholder:text-faint "
                    "focus:outline-none focus:border-spark"})) {
        cerco_raw(r, "");
      }
      cerco_tag(r, "button", CERCO_ATTRS(
          {"type", "submit"},
          {"class", "mt-6 rounded-lg bg-spark px-5 py-2.5 font-mono text-[11px] "
                    "uppercase tracking-[0.18em] text-void hover:opacity-90 "
                    "transition-opacity"})) {
        cerco_raw(r, "sign the guestbook");
      }
    }

    cerco_tag(r, "div", CERCO_CLASS("mt-14 border-t border-edge")) {
      if (n == 0) {
        cerco_tag(r, "p", CERCO_CLASS("py-8 text-sm text-faint")) {
          cerco_raw(r, "No entries yet &mdash; be the first.");
        }
      }
      for (size_t i = 0; i < n; i++) {
        cerco_tag(r, "article", CERCO_CLASS("border-b border-edge py-5")) {
          cerco_tag(r, "header", CERCO_CLASS(
              "flex items-baseline justify-between gap-4")) {
            cerco_tag(r, "span", CERCO_CLASS("font-medium tracking-tight")) {
              cerco_text(r, entries[i].name);
            }
            cerco_tag(r, "span", CERCO_CLASS(
                "font-mono text-[11px] uppercase tracking-[0.18em] text-faint")) {
              cerco_text(r, entries[i].when);
            }
          }
          cerco_tag(r, "p", CERCO_CLASS("mt-2 leading-relaxed text-dim")) {
            cerco_text(r, entries[i].message);
          }
        }
      }
    }
  }
}
