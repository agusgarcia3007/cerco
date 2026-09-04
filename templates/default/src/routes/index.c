#include <cerco.h>
#include "server/pokedex_view.h"

CERCO_ROUTE {
  cerco_title(r, "Pokedex · my-app");

  /* ---- hero ---- */
  cerco_tag(r, "div", CERCO_CLASS("hero-glow")) {
    cerco_tag(r, "div", CERCO_CLASS("mx-auto max-w-5xl px-6 pt-20 pb-16 text-center")) {
      cerco_tag(r, "p", CERCO_CLASS(
          "inline-block rounded-full border border-edge bg-panel px-3 py-1 "
          "font-mono text-[11px] uppercase tracking-[0.18em] text-dim")) {
        cerco_raw(r, "a cerco starter");
      }
      cerco_tag(r, "h1", CERCO_CLASS(
          "mt-6 text-5xl sm:text-6xl font-semibold tracking-tight leading-[1.05] "
          "bg-gradient-to-b from-white to-zinc-500 bg-clip-text text-transparent")) {
        cerco_raw(r, "One language.<br>One native binary.");
      }
      cerco_tag(r, "p", CERCO_CLASS("mt-6 text-lg text-dim max-w-xl mx-auto")) {
        cerco_raw(r, "Get started by editing "
                     "<code class=\"font-mono text-sm text-ink\">src/routes/index.c</code>"
                     " &mdash; the grid below is already live: 151 Pokémon fetched "
                     "from the PokeAPI by the wasm client.");
      }
    }
  }

  /* ---- pokedex: fetch + signals + events, all in wasm ---- */
  pokedex_grid(r);

  /* ---- feature cards ---- */
  cerco_tag(r, "div", CERCO_CLASS("mx-auto max-w-5xl px-6 pb-24")) {
    cerco_tag(r, "div", CERCO_CLASS("grid gap-4 sm:grid-cols-3")) {
      cerco_tag(r, "a", CERCO_ATTRS(
          {"href", "/pokemon/pikachu"},
          {"class", "group rounded-xl border border-edge bg-panel p-6 "
                    "hover:border-faint transition-colors"})) {
        cerco_tag(r, "h2", CERCO_CLASS("font-semibold tracking-tight")) {
          cerco_raw(r, "Dynamic routes ");
          cerco_tag(r, "span", CERCO_CLASS(
              "inline-block transition-transform group-hover:translate-x-1 text-spark")) {
            cerco_raw(r, "&rarr;");
          }
        }
        cerco_tag(r, "p", CERCO_CLASS("mt-2 text-sm leading-relaxed text-dim")) {
          cerco_raw(r, "<code class=\"font-mono text-xs\">src/routes/pokemon/[name].c</code> "
                       "answers /pokemon/&lt;anything&gt; and reads the parameter "
                       "server-side.");
        }
      }
      cerco_tag(r, "a", CERCO_ATTRS(
          {"href", "/demo"},
          {"class", "group rounded-xl border border-edge bg-panel p-6 "
                    "hover:border-faint transition-colors"})) {
        cerco_tag(r, "h2", CERCO_CLASS("font-semibold tracking-tight")) {
          cerco_raw(r, "Wasm client ");
          cerco_tag(r, "span", CERCO_CLASS(
              "inline-block transition-transform group-hover:translate-x-1 text-spark")) {
            cerco_raw(r, "&rarr;");
          }
        }
        cerco_tag(r, "p", CERCO_CLASS("mt-2 text-sm leading-relaxed text-dim")) {
          cerco_raw(r, "Signals, events and server functions &mdash; C compiled to "
                       "WebAssembly, hydrated over the SSR markup.");
        }
      }
      cerco_tag(r, "a", CERCO_ATTRS(
          {"href", "/guestbook"},
          {"class", "group rounded-xl border border-edge bg-panel p-6 "
                    "hover:border-faint transition-colors"})) {
        cerco_tag(r, "h2", CERCO_CLASS("font-semibold tracking-tight")) {
          cerco_raw(r, "Forms ");
          cerco_tag(r, "span", CERCO_CLASS(
              "inline-block transition-transform group-hover:translate-x-1 text-spark")) {
            cerco_raw(r, "&rarr;");
          }
        }
        cerco_tag(r, "p", CERCO_CLASS("mt-2 text-sm leading-relaxed text-dim")) {
          cerco_raw(r, "A POST handler with validation and redirect-after-submit, "
                       "no JavaScript involved.");
        }
      }
    }
  }
}
