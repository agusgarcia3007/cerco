/* server-side markup for /pokemon/<name> (server-only).
 * The matching client code lives in src/components/pokemon.c. */
#include <cerco.h>
#include <stdio.h>

static const char *pokemon_stat_names[6] = {
  "hp", "attack", "defense", "sp. atk", "sp. def", "speed"
};

static void pokemon_not_found(cerco_req *r, const char *name) {
  cerco_tag(r, "div", CERCO_CLASS("mx-auto max-w-5xl px-6 py-24")) {
    cerco_tag(r, "p", CERCO_CLASS(
        "font-mono text-[11px] uppercase tracking-[0.18em] text-spark")) {
      cerco_raw(r, "404");
    }
    cerco_tag(r, "h1", CERCO_CLASS("mt-4 text-3xl font-semibold tracking-tight")) {
      cerco_textf(r, "%s is not a pokemon name.", name && name[0] ? name : "that");
    }
    cerco_tag(r, "p", CERCO_CLASS("mt-4 text-dim max-w-lg leading-relaxed")) {
      cerco_raw(r, "Names are lowercase letters, digits and dashes &mdash; the "
                   "route rejects anything else before it reaches the client.");
    }
    cerco_tag(r, "p", CERCO_CLASS("mt-8 text-sm")) {
      cerco_tag(r, "a", CERCO_ATTRS(
          {"href", "/"}, {"class", "text-spark hover:underline"})) {
        cerco_raw(r, "&larr; back to the pokedex");
      }
    }
  }
}

static void pokemon_detail(cerco_req *r, const char *name, const char *props) {
  cerco_tag(r, "div", CERCO_CLASS("mx-auto max-w-3xl px-6 py-16")) {
    cerco_tag(r, "a", CERCO_ATTRS(
        {"href", "/"},
        {"class", "font-mono text-[11px] uppercase tracking-[0.18em] "
                  "text-faint hover:text-spark transition-colors"})) {
      cerco_raw(r, "&larr; pokedex");
    }

    cerco_tag(r, "div", CERCO_COMPONENT(r, "pokemon", props)) {
      cerco_tag(r, "p", CERCO_ATTRS(
          {"data-cerco-b", "error"},
          {"class", "hidden mt-8 rounded-lg border border-spark bg-panel "
                    "px-4 py-3 font-mono text-sm text-spark"})) {
        cerco_raw(r, "");
      }

      cerco_tag(r, "div", CERCO_CLASS(
          "mt-8 flex flex-col sm:flex-row sm:items-center gap-8")) {
        cerco_void_tag(r, "img", CERCO_ATTRS(
            {"data-cerco-b", "sprite"},
            {"alt", name},
            {"class", "h-40 w-40 shrink-0 self-center rounded-xl border "
                      "border-edge bg-panel p-2"}));
        cerco_tag(r, "div", NULL) {
          cerco_tag(r, "p", CERCO_ATTRS(
              {"data-cerco-b", "number"},
              {"class", "font-mono text-xs text-faint"})) {
            cerco_raw(r, "#&mdash;");
          }
          /* the name is known server-side: it renders before any JS runs */
          cerco_tag(r, "h1", CERCO_CLASS(
              "mt-1 text-4xl font-semibold tracking-tight capitalize")) {
            cerco_text(r, name);
          }
          cerco_tag(r, "div", CERCO_ATTRS(
              {"data-cerco-b", "types"},
              {"class", "mt-4 flex flex-wrap gap-2"})) {
            cerco_raw(r, "");
          }
        }
      }

      cerco_tag(r, "div", CERCO_CLASS(
          "mt-12 rounded-xl border border-edge bg-panel p-6")) {
        cerco_tag(r, "h2", CERCO_CLASS(
            "font-mono text-[11px] uppercase tracking-[0.18em] text-faint")) {
          cerco_raw(r, "base stats");
        }
        for (int i = 0; i < 6; i++) {
          char bar[16], val[16];
          snprintf(bar, sizeof(bar), "bar%d", i);
          snprintf(val, sizeof(val), "val%d", i);
          cerco_tag(r, "div", CERCO_CLASS("mt-4 flex items-center gap-4")) {
            cerco_tag(r, "span", CERCO_CLASS(
                "w-20 shrink-0 font-mono text-[11px] uppercase "
                "tracking-[0.14em] text-dim")) {
              cerco_text(r, pokemon_stat_names[i]);
            }
            cerco_tag(r, "span", CERCO_ATTRS(
                {"data-cerco-b", val},
                {"class", "w-8 shrink-0 font-mono text-xs text-ink"})) {
              cerco_raw(r, "&mdash;");
            }
            cerco_tag(r, "span", CERCO_CLASS(
                "h-1.5 grow rounded-full bg-edge overflow-hidden")) {
              cerco_tag(r, "span", CERCO_ATTRS(
                  {"data-cerco-b", bar},
                  {"style", "width:0%"},
                  {"class", "block h-full rounded-full bg-spark "
                            "transition-[width] duration-500"})) {
                cerco_raw(r, "");
              }
            }
          }
        }
      }

      cerco_tag(r, "p", CERCO_CLASS("mt-8 text-sm leading-relaxed text-dim")) {
        cerco_raw(r, "The name above is server-rendered from the route "
                     "parameter; the sprite, types and stats are fetched by "
                     "<code class=\"font-mono text-xs text-ink\">"
                     "src/components/pokemon.c</code> running in WebAssembly.");
      }
    }
  }
}
