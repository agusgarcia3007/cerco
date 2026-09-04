/* pokedex — the client-side star of the starter.
 *
 * On mount it fetches the first 151 Pokémon from pokeapi.co (a plain
 * CORS-enabled JSON API), builds one card per entry through the DOM command
 * buffer, and wires a filter input. Everything in this file runs in
 * WebAssembly: no JavaScript was written for this app.
 */
#include <cerco_client.h>
#include "json_scan.h"

#define POKE_LIST_URL "https://pokeapi.co/api/v2/pokemon?limit=151"
#define POKE_CAP 160
#define NAME_LEN 24

typedef struct {
  char name[NAME_LEN];
  int id;
} poke_entry;

typedef struct {
  poke_entry entries[POKE_CAP];
  int n;
  char filter[32];
  int32_t grid, status, count;
} pokedex_state;

/* one mount per page; the client heap rewinds on navigation */
static pokedex_state *g;

/* ------------------------------------------------------------------ parse */

/* the list response is {"results":[{"name":..,"url":..}, ...]} — walk it by
 * reading name/url pairs in order until the keys run out */
static int parse_list(const char *body, poke_entry *out, int cap) {
  const char *p = strstr(body, "\"results\"");
  if (!p) return 0;
  int n = 0;
  while (n < cap) {
    char name[NAME_LEN], url[128];
    const char *after = cj_find_str(p, "name", name, (int)sizeof(name));
    if (!after) break;
    p = cj_find_str(after, "url", url, (int)sizeof(url));
    if (!p) break;
    poke_entry *e = &out[n++];
    strcpy(e->name, name);
    e->id = cj_url_tail_int(url);
  }
  return n;
}

/* ------------------------------------------------------------------ render */

static void title_case(char *dst, const char *src) {
  strcpy(dst, src);
  if (dst[0] >= 'a' && dst[0] <= 'z') dst[0] = (char)(dst[0] - 32);
}

/* cards are built as real nodes through the command buffer — not innerHTML
 * per card, which would re-parse the whole grid on every keystroke */
static void render_card(const poke_entry *e) {
  char display[NAME_LEN], buf[192];
  title_case(display, e->name);

  int32_t a = cerco_create("a", g->grid);
  cerco_format(buf, (int32_t)sizeof(buf), "/pokemon/%s", e->name);
  cerco_set_attr(a, "href", buf);
  cerco_set_attr(a, "class",
                 "group rounded-xl border border-edge bg-panel p-4 text-center "
                 "hover:border-faint transition-colors");

  int32_t img = cerco_create("img", a);
  cerco_format(buf, (int32_t)sizeof(buf),
               "https://raw.githubusercontent.com/PokeAPI/sprites/master"
               "/sprites/pokemon/%d.png", e->id);
  cerco_set_attr(img, "src", buf);
  cerco_set_attr(img, "alt", display);
  cerco_set_attr(img, "loading", "lazy");
  cerco_set_attr(img, "class", "h-20 w-20 mx-auto [image-rendering:pixelated]");

  int32_t num = cerco_create("p", a);
  cerco_set_attr(num, "class", "mt-2 font-mono text-[11px] text-faint");
  cerco_format(buf, (int32_t)sizeof(buf), "#%d", e->id);
  cerco_set_text(num, buf);

  int32_t label = cerco_create("p", a);
  cerco_set_attr(label, "class",
                 "mt-0.5 text-sm font-medium tracking-tight "
                 "group-hover:text-spark transition-colors");
  cerco_set_text(label, display);
}

/* case-insensitive substring match */
static int matches(const char *name, const char *q) {
  if (!q[0]) return 1;
  for (int a = 0; name[a]; a++) {
    int b = 0;
    while (q[b] && name[a + b]) {
      char x = q[b], y = name[a + b];
      if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
      if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
      if (x != y) break;
      b++;
    }
    if (!q[b]) return 1;
  }
  return 0;
}

static void render_filtered(void) {
  cerco_set_inner_html(g->grid, "");
  int shown = 0;
  for (int i = 0; i < g->n; i++) {
    if (!matches(g->entries[i].name, g->filter)) continue;
    render_card(&g->entries[i]);
    shown++;
  }
  char buf[32];
  cerco_format(buf, (int32_t)sizeof(buf), "%d of %d", shown, g->n);
  cerco_set_text(g->count, buf);
  cerco_set_text(g->status, shown ? "" : "no pokemon matches that name");
}

/* ------------------------------------------------------------ events + net */

static void on_filter(int32_t node, void *user) {
  char buf[32];
  int32_t n = cerco_value(node, buf, (int32_t)sizeof(buf) - 1);
  if (n < 0) n = 0;
  buf[n] = 0;
  strcpy(g->filter, buf);
  render_filtered();
}

static void on_list(int status, const uint8_t *data, int32_t len, void *user) {
  if (status == CERCO_HTTP_TOO_LARGE) {
    cerco_set_text(g->status, "the pokedex response did not fit the fetch buffer");
    return;
  }
  if (status != 200 || !data || len <= 0) {
    cerco_set_text(g->status, "pokeapi.co is unreachable — check your connection");
    return;
  }
  g->n = parse_list((const char *)data, g->entries, POKE_CAP);
  if (g->n == 0) {
    cerco_set_text(g->status, "could not read the pokedex response");
    return;
  }
  render_filtered();
}

CERCO_CLIENT_COMPONENT(pokedex) {
  int32_t root = cerco_root_node(cerco_root);
  g = (pokedex_state *)cerco_alloc(sizeof(pokedex_state));
  g->n = 0;
  g->filter[0] = 0;
  g->grid = cerco_query(root, "[data-cerco-b=grid]");
  g->status = cerco_query(root, "[data-cerco-b=status]");
  g->count = cerco_query(root, "[data-cerco-b=count]");
  cerco_on(cerco_query(root, "[data-cerco-b=filter]"), "input", on_filter, 0);
  cerco_http_get(POKE_LIST_URL, on_list, 0);
}
