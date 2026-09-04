/* pokemon — detail view for /pokemon/<name>.
 *
 * The route renders the shell server-side and passes the name through
 * component props; this component fetches the one Pokémon from pokeapi.co
 * and fills the sprite, types and base stats in.
 */
#include <cerco_client.h>
#include "json_scan.h"

#define STAT_COUNT 6
#define STAT_MAX 255 /* the highest base stat in the games; bars scale to it */

typedef struct {
  int32_t sprite, number, types, error;
  int32_t bar[STAT_COUNT], value[STAT_COUNT];
} pokemon_state;

static pokemon_state *g;

static void fail(const char *msg) {
  cerco_set_text(g->error, msg);
  cerco_remove_class(g->error, "hidden");
}

static void on_detail(int status, const uint8_t *data, int32_t len, void *user) {
  if (status == 404) {
    fail("no pokemon by that name — check the spelling in the URL");
    return;
  }
  if (status == CERCO_HTTP_TOO_LARGE) {
    /* raise the ceiling with -DCERCO_FETCH_MAX=... if an API needs it */
    fail("the response was larger than the client's fetch buffer");
    return;
  }
  if (status != 200 || !data || len <= 0) {
    fail("pokeapi.co is unreachable — check your connection");
    return;
  }
  const char *body = (const char *)data;
  char buf[192];

  int id = cj_find_int(body, "id", 0);
  cerco_format(buf, (int32_t)sizeof(buf), "#%d", id);
  cerco_set_text(g->number, buf);

  cerco_format(buf, (int32_t)sizeof(buf),
               "https://raw.githubusercontent.com/PokeAPI/sprites/master"
               "/sprites/pokemon/other/official-artwork/%d.png", id);
  cerco_set_attr(g->sprite, "src", buf);

  /* types: each entry is {"slot":n,"type":{"name":"fire",...}} */
  const char *p = strstr(body, "\"types\"");
  for (int i = 0; p && i < 2; i++) { /* a pokemon has at most two types */
    char name[24];
    p = cj_seek(p, "type");
    if (!p) break;
    p = cj_find_str(p, "name", name, (int)sizeof(name));
    if (!p) break;
    int32_t chip = cerco_create("span", g->types);
    cerco_set_attr(chip, "class",
                   "rounded-full border border-edge bg-panel px-3 py-1 "
                   "font-mono text-[11px] uppercase tracking-[0.14em] text-dim");
    cerco_set_text(chip, name);
  }

  /* stats come back in the fixed game order: hp, attack, defense,
   * special-attack, special-defense, speed */
  const char *s = body;
  for (int i = 0; i < STAT_COUNT; i++) {
    const char *after = cj_seek(s, "base_stat");
    if (!after) break;
    int v = cj_find_int(s, "base_stat", 0);
    s = after;
    cerco_format(buf, (int32_t)sizeof(buf), "%d", v);
    cerco_set_text(g->value[i], buf);
    cerco_format(buf, (int32_t)sizeof(buf), "width:%d%%", v * 100 / STAT_MAX);
    cerco_set_attr(g->bar[i], "style", buf);
  }
}

CERCO_CLIENT_COMPONENT(pokemon) {
  int32_t root = cerco_root_node(cerco_root);
  g = (pokemon_state *)cerco_alloc(sizeof(pokemon_state));
  g->sprite = cerco_query(root, "[data-cerco-b=sprite]");
  g->number = cerco_query(root, "[data-cerco-b=number]");
  g->types = cerco_query(root, "[data-cerco-b=types]");
  g->error = cerco_query(root, "[data-cerco-b=error]");
  for (int i = 0; i < STAT_COUNT; i++) {
    char sel[40];
    cerco_format(sel, (int32_t)sizeof(sel), "[data-cerco-b=bar%d]", i);
    g->bar[i] = cerco_query(root, sel);
    cerco_format(sel, (int32_t)sizeof(sel), "[data-cerco-b=val%d]", i);
    g->value[i] = cerco_query(root, sel);
  }

  char name[32], url[128];
  if (!cerco_json_str(cerco_props, "name", name, (int32_t)sizeof(name))) {
    fail("the route did not pass a name");
    return;
  }
  cerco_format(url, (int32_t)sizeof(url),
               "https://pokeapi.co/api/v2/pokemon/%s", name);
  cerco_http_get(url, on_detail, 0);
}
