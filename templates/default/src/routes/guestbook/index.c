#include <cerco.h>
#include <stdio.h>
#include "server/guestbook.h"

CERCO_ROUTE {
  size_t n = 0;
  const guestbook_entry *entries = guestbook_entries(&n);

  cerco_tag(r, "p", CERCO_CLASS("label")) { cerco_raw(r, "Guestbook"); }
  cerco_tag(r, "h1", CERCO_CLASS("mt-6 font-display text-4xl tracking-tight")) {
    cerco_raw(r, "Say something.");
  }
  cerco_tag(r, "p", CERCO_CLASS("mt-4 text-[15px] leading-relaxed text-faded max-w-xl")) {
    cerco_raw(r, "Entries live in server memory only &mdash; a restart clears them. "
                 "The point of this page is the flow: a GET renders the form, "
                 "<code class=\"font-mono text-xs\">index.post.c</code> validates and stores, "
                 "then redirects back so a refresh never double-posts.");
  }

  if (cerco_query_get(r, "e")) {
    cerco_tag(r, "p", CERCO_CLASS(
        "mt-6 border border-rust px-4 py-3 text-sm text-rust font-mono")) {
      cerco_raw(r, "both fields are required &mdash; try again");
    }
  }

  cerco_tag(r, "form", CERCO_ATTRS(
      {"method", "post"}, {"action", "/guestbook"}, {"class", "mt-10 max-w-xl"})) {
    cerco_tag(r, "label", CERCO_ATTRS({"for", "name"}, {"class", "label block"})) {
      cerco_raw(r, "Name");
    }
    cerco_void_tag(r, "input", CERCO_ATTRS(
        {"id", "name"}, {"name", "name"}, {"required", "required"},
        {"maxlength", "40"},
        {"class", "mt-2 w-full bg-transparent border border-rule px-3 py-2 "
                  "text-[15px] focus:outline-none focus:border-rust"}));
    cerco_tag(r, "label", CERCO_ATTRS({"for", "message"}, {"class", "label block mt-5"})) {
      cerco_raw(r, "Message");
    }
    cerco_tag(r, "textarea", CERCO_ATTRS(
        {"id", "message"}, {"name", "message"}, {"rows", "3"},
        {"required", "required"}, {"maxlength", "280"},
        {"class", "mt-2 w-full bg-transparent border border-rule px-3 py-2 "
                  "text-[15px] leading-relaxed focus:outline-none focus:border-rust"})) {
    }
    cerco_tag(r, "button", CERCO_ATTRS(
        {"type", "submit"},
        {"class", "mt-6 font-mono text-xs uppercase tracking-[0.18em] border "
                  "border-ink px-5 py-2.5 hover:bg-ink hover:text-paper "
                  "transition-colors"})) {
      cerco_raw(r, "Sign the guestbook");
    }
  }

  cerco_tag(r, "div", CERCO_CLASS("mt-14 border-t border-rule")) {
    if (n == 0) {
      cerco_tag(r, "p", CERCO_CLASS("py-6 italic text-faded")) {
        cerco_raw(r, "No entries yet &mdash; be the first.");
      }
    }
    for (size_t i = 0; i < n; i++) {
      cerco_tag(r, "article", CERCO_CLASS("border-b border-rule py-5")) {
        cerco_tag(r, "header", CERCO_CLASS("flex items-baseline justify-between gap-4")) {
          cerco_tag(r, "span", CERCO_CLASS("font-display italic text-lg")) {
            cerco_text(r, entries[i].name);
          }
          cerco_tag(r, "span", CERCO_CLASS("label")) {
            cerco_text(r, entries[i].when);
          }
        }
        cerco_tag(r, "p", CERCO_CLASS("mt-2 text-[15px] leading-relaxed")) {
          cerco_text(r, entries[i].message);
        }
      }
    }
  }
}
