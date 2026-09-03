#define _POSIX_C_SOURCE 200809L
#include "main.h"
#include "util.h"
#include "sha256.h"
#include "str.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

/* ----------------------------------------------------------------- models */

#define MAX_ROUTES 256
#define MAX_LAYOUTS 64

typedef struct {
  char method[16];
  char path[256];      /* url path: /users/:id */
  char symbol[160];    /* mangled handler symbol */
  char rel[512];       /* rel path from project root */
  char dir[256];       /* dir rel to src/routes ("" = root) */
  char layouts[MAX_LAYOUTS][160]; /* layout symbols, root -> leaf */
  int n_layouts;
} route_info;

typedef struct {
  char dir[256];
  char symbol[160];
} layout_info;

typedef struct {
  uint32_t id;
  char name[128];
  char method[16];
  char ret;            /* i l f b s y */
  char args[16];       /* type chars */
  int n_args;
} sf_info;

typedef struct {
  cerco_project *proj;
  char sdk[1200];
  char gendir[1200];
  char objdir[1200];
  char distdir[1200];
  int release;
  int dev_mode;
  int dev_assets;   /* no embedding: dev server serves dist/ from disk */

  route_info routes[MAX_ROUTES];
  int n_routes;
  layout_info layouts[MAX_LAYOUTS];
  int n_layouts;
  sf_info sfs[MAX_ROUTES];
  int n_sfs;
  char components[MAX_ROUTES][128];
  int n_components;
} build_ctx;

static void mangle(const char *in, char *out, size_t cap) {
  size_t o = 0;
  int prev_us = 1;
  for (const char *p = in; *p && o + 1 < cap; p++) {
    char c = *p;
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
      out[o++] = c;
      prev_us = 0;
    } else if (!prev_us) {
      out[o++] = '_';
      prev_us = 1;
    }
  }
  if (o && out[o - 1] == '_') o--;
  out[o] = 0;
}

/* ----------------------------------------------------------- route scanning */

typedef struct {
  build_ctx *b;
  char routes_root[1200];
} scan_ctx;

static void scan_cb(const char *rel, const char *full, int is_dir, void *user) {
  scan_ctx *s = (scan_ctx *)user;
  build_ctx *b = s->b;
  if (is_dir) return;
  size_t rl = strlen(rel);
  if (rl < 2 || strcmp(rel + rl - 2, ".c") != 0) return;

  /* dir = dirname of rel */
  char dirbuf[512];
  snprintf(dirbuf, sizeof(dirbuf), "%s", rel);
  char *slash = strrchr(dirbuf, '/');
  const char *base;
  if (slash) { *slash = 0; base = slash + 1; }
  else { dirbuf[0] = 0; base = rel; }

  /* method suffix (checked after removing the .c extension) */
  char basebuf[256];
  snprintf(basebuf, sizeof(basebuf), "%s", base);
  size_t bl0 = strlen(basebuf);
  if (bl0 > 2 && !strcmp(basebuf + bl0 - 2, ".c")) basebuf[bl0 - 2] = 0;
  char method[16] = "GET";
  static const char *methods[] = { "get", "post", "put", "patch", "delete", "head", "options" };
  for (int i = 0; i < 7; i++) {
    char suffix[32];
    snprintf(suffix, sizeof(suffix), ".%s", methods[i]);
    size_t bl = strlen(basebuf);
    size_t sl = strlen(suffix);
    if (bl > sl && strcmp(basebuf + bl - sl, suffix) == 0) {
      snprintf(method, sizeof(method), "%s", methods[i]);
      for (char *m = method; *m; m++) if (*m >= 'a' && *m <= 'z') *m = (char)(*m - 32);
      basebuf[bl - sl] = 0;
      break;
    }
  }

  if (!strcmp(basebuf, "layout") || !strcmp(basebuf, "layout.c")) {
    if (b->n_layouts >= MAX_LAYOUTS) return;
    layout_info *l = &b->layouts[b->n_layouts++];
    snprintf(l->dir, sizeof(l->dir), "%s", dirbuf);
    char m[256];
    mangle(dirbuf[0] ? dirbuf : "root", m, sizeof(m));
    snprintf(l->symbol, sizeof(l->symbol), "cerco_layout_%s", m);
    return;
  }

  if (b->n_routes >= MAX_ROUTES) {
    fprintf(stderr, "cerco: too many routes (max %d)\n", MAX_ROUTES);
    return;
  }
  route_info *r = &b->routes[b->n_routes];
  memset(r, 0, sizeof(*r));
  snprintf(r->method, sizeof(r->method), "%s", method);
  snprintf(r->rel, sizeof(r->rel), "src/routes/%s", rel);
  snprintf(r->dir, sizeof(r->dir), "%s", dirbuf);

  /* url path */
  /* strip ".c" from basebuf (it was already split; base may end with .c) */
  char fb[256];
  snprintf(fb, sizeof(fb), "%s", basebuf);
  size_t fl = strlen(fb);
  if (fl > 2 && !strcmp(fb + fl - 2, ".c")) fb[fl - 2] = 0;

  char seg[256];
  if (!strcmp(fb, "index") || !strcmp(fb, "index.get") || !strcmp(fb, "")) {
    seg[0] = 0;
  } else {
    /* [id] -> :id ; [...slug] -> *slug */
    snprintf(seg, sizeof(seg), "%s", fb);
    size_t sl = strlen(seg);
    if (sl > 4 && !strcmp(seg + sl - 2, ".c")) seg[sl - 2] = 0;
    if (seg[0] == '[') {
      if (seg[1] == '.' && seg[2] == '.' && seg[3] == '.') {
        char nm[256];
        snprintf(nm, sizeof(nm), "*%s", seg + 4);
        size_t nl = strlen(nm);
        if (nl && nm[nl - 1] == ']') nm[nl - 1] = 0;
        snprintf(seg, sizeof(seg), "%s", nm);
      } else {
        char nm[256];
        snprintf(nm, sizeof(nm), ":%s", seg + 1);
        size_t nl = strlen(nm);
        if (nl && nm[nl - 1] == ']') nm[nl - 1] = 0;
        snprintf(seg, sizeof(seg), "%s", nm);
      }
    }
  }
  if (!strcmp(basebuf, "404.c")) {
    snprintf(r->path, sizeof(r->path), "/__cerco/404");
  } else if (seg[0] == 0) {
    snprintf(r->path, sizeof(r->path), "%s%s", r->dir[0] ? "/" : "",
             r->dir[0] ? r->dir : "");
    if (!r->path[0]) snprintf(r->path, sizeof(r->path), "/");
  } else {
    snprintf(r->path, sizeof(r->path), "/%s%s%s", r->dir,
             r->dir[0] ? "/" : "", seg);
  }

  /* symbol */
  char ms[512];
  snprintf(ms, sizeof(ms), "%s%s%s", r->dir, r->dir[0] ? "_" : "", fb);
  char mg[512];
  mangle(ms, mg, sizeof(mg));
  snprintf(r->symbol, sizeof(r->symbol), "cerco_route_%s", mg);
  /* dedupe symbols */
  for (int i = 0; i < b->n_routes - 1; i++) {
    if (!strcmp(b->routes[i].symbol, r->symbol)) {
      snprintf(r->symbol, sizeof(r->symbol), "%s_%d", r->symbol, b->n_routes);
      break;
    }
  }
  b->n_routes++;
}

static int find_layout(build_ctx *b, const char *dir, char *out, size_t cap) {
  for (int i = 0; i < b->n_layouts; i++) {
    if (!strcmp(b->layouts[i].dir, dir)) {
      snprintf(out, cap, "%s", b->layouts[i].symbol);
      return 1;
    }
  }
  return 0;
}

static void compute_layout_chains(build_ctx *b) {
  for (int i = 0; i < b->n_routes; i++) {
    route_info *r = &b->routes[i];
    /* walk ancestors from root to route dir (root dir "" included) */
    char dirs[MAX_LAYOUTS][256];
    int nd = 0;
    char work[256];
    snprintf(work, sizeof(work), "%s", r->dir);
    for (;;) {
      snprintf(dirs[nd < MAX_LAYOUTS ? nd : MAX_LAYOUTS - 1], 256, "%s", work);
      nd++;
      char *slash = strrchr(work, '/');
      if (!slash) break;
      *slash = 0;
    }
    for (int k = nd - 1; k >= 0 && r->n_layouts < MAX_LAYOUTS; k--) {
      char sym[160];
      if (find_layout(b, dirs[k], sym, sizeof(sym))) {
        snprintf(r->layouts[r->n_layouts++], 160, "%s", sym);
      }
    }
  }
}

/* ----------------------------------------------------------- sf + components */

static int scan_sf(build_ctx *b) {
  char path[1400];
  snprintf(path, sizeof(path), "%s/src/server/functions.x", b->proj->root);
  if (!file_exists(path)) {
    snprintf(path, sizeof(path), "%s/src/shared/functions.x", b->proj->root);
    if (!file_exists(path)) return 0;
  }
  size_t len = 0;
  char *data = read_file(path, &len);
  if (!data) return -1;
  char *saveptr = NULL;
  for (char *line = strtok_r(data, "\n", &saveptr); line;
       line = strtok_r(NULL, "\n", &saveptr)) {
    char *t = trim(line);
    if (!t[0] || t[0] == '#' || t[0] == '/' || t[0] == '*') continue;
    const char *prefix = "CERCO_SF(";
    if (strncmp(t, prefix, strlen(prefix)) != 0) continue;
    char body[512];
    const char *start = t + strlen(prefix);
    const char *end = strrchr(start, ')');
    if (!end) continue;
    size_t bl = (size_t)(end - start);
    if (bl >= sizeof(body)) continue;
    memcpy(body, start, bl);
    body[bl] = 0;

    if (b->n_sfs >= MAX_ROUTES) break;
    sf_info *sf = &b->sfs[b->n_sfs];
    memset(sf, 0, sizeof(*sf));
    snprintf(sf->method, sizeof(sf->method), "POST");

    char *save = NULL;
    int field = 0;
    for (char *tok = strtok_r(body, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
      char *v = trim(tok);
      field++;
      if (field == 1) {
        sf->id = (uint32_t)strtoul(v, NULL, 10);
      } else if (field == 2) {
        snprintf(sf->name, sizeof(sf->name), "%s", v);
      } else if (field == 3) {
        sf->ret = !strcmp(v, "CERCO_I32") ? 'i' : !strcmp(v, "CERCO_I64") ? 'l'
                : !strcmp(v, "CERCO_F64") ? 'f' : !strcmp(v, "CERCO_BOOL") ? 'b'
                : !strcmp(v, "CERCO_STR") ? 's' : !strcmp(v, "CERCO_BYTES") ? 'y' : '?';
      } else {
        char c = !strcmp(v, "CERCO_I32") ? 'i' : !strcmp(v, "CERCO_I64") ? 'l'
               : !strcmp(v, "CERCO_F64") ? 'f' : !strcmp(v, "CERCO_BOOL") ? 'b'
               : !strcmp(v, "CERCO_STR") ? 's' : !strcmp(v, "CERCO_BYTES") ? 'y' : '?';
        if (sf->n_args < 16) sf->args[sf->n_args++] = c;
      }
    }
    if (field >= 3 && sf->ret != '?') b->n_sfs++;
  }
  free(data);
  return 0;
}

static void scan_components_cb(const char *rel, const char *full, int is_dir, void *user) {
  build_ctx *b = (build_ctx *)user;
  if (is_dir) return;
  if (strncmp(rel, "src/components/", 15) != 0) return;
  size_t rl = strlen(rel);
  if (rl < 2 || strcmp(rel + rl - 2, ".c") != 0) return;
  size_t len = 0;
  char *data = read_file(full, &len);
  if (!data) return;
  const char *p = data;
  const char *marker = "CERCO_CLIENT_COMPONENT(";
  while ((p = strstr(p, marker)) != NULL) {
    p += strlen(marker);
    char name[128];
    size_t i = 0;
    while (*p && *p != ')' && *p != ',' && i + 1 < sizeof(name)) name[i++] = *p++;
    name[i] = 0;
    char *nm = trim(name);
    if (nm[0] && b->n_components < MAX_ROUTES) {
      snprintf(b->components[b->n_components++], 128, "%s", nm);
    }
  }
  free(data);
}

/* --------------------------------------------------------------- codegen */

static int gen_routes(build_ctx *b) {
  char path[1400];
  snprintf(path, sizeof(path), "%s/routes.c", b->gendir);
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "/* generated by cerco — do not edit */\n");
  fprintf(f, "#include \"cerco.h\"\n\n");
  for (int i = 0; i < b->n_routes; i++) {
    route_info *r = &b->routes[i];
    fprintf(f, "extern void %s(cerco_req *r);\n", r->symbol);
  }
  for (int i = 0; i < b->n_layouts; i++) {
    layout_info *l = &b->layouts[i];
    fprintf(f, "extern void %s(cerco_req *r, void (*children)(cerco_req *));\n",
            l->symbol);
  }
  fprintf(f, "\n");
  for (int i = 0; i < b->n_routes; i++) {
    route_info *r = &b->routes[i];
    fprintf(f, "static void page_%d(cerco_req *r) {\n", i);
    fprintf(f, "  cerco_raw(r, \"<!--cerco:page-->\");\n");
    fprintf(f, "  %s(r);\n", r->symbol);
    fprintf(f, "  cerco_raw(r, \"<!--cerco:/page-->\");\n");
    fprintf(f, "}\n");
    /* layout chain wrappers: outermost -> ... -> page */
    for (int k = r->n_layouts; k >= 1; k--) {
      int level = r->n_layouts - k + 1;
      if (level == 1) {
        fprintf(f, "static void chain_%d_l%d(cerco_req *r) { %s(r, page_%d); }\n",
                i, level, r->layouts[k - 1], i);
      } else {
        fprintf(f, "static void chain_%d_l%d(cerco_req *r) { %s(r, chain_%d_l%d); }\n",
                i, level, r->layouts[k - 1], i, level - 1);
      }
    }
  }
  fprintf(f, "\nconst cerco_route_entry cerco_routes_table[] = {\n");
  for (int i = 0; i < b->n_routes; i++) {
    route_info *r = &b->routes[i];
    if (r->n_layouts) {
      fprintf(f, "  { \"%s\", \"%s\", chain_%d_l%d },\n", r->method, r->path, i,
              r->n_layouts);
    } else {
      fprintf(f, "  { \"%s\", \"%s\", page_%d },\n", r->method, r->path, i);
    }
  }
  fprintf(f, "};\n");
  fprintf(f, "const unsigned long cerco_routes_count = %d;\n", b->n_routes);
  /* root layout symbol */
  char rootsym[160] = "";
  find_layout(b, "", rootsym, sizeof(rootsym));
  fprintf(f, "const cerco_layout_fn cerco_root_layout_sym = %s;\n",
          rootsym[0] ? rootsym : "0");
  fclose(f);
  return 0;
}

static int gen_main(build_ctx *b) {
  char path[1400];
  snprintf(path, sizeof(path), "%s/main.c", b->gendir);
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "/* generated by cerco — do not edit */\n");
  fprintf(f, "#include \"cerco.h\"\n\n");
  fprintf(f, "extern const cerco_route_entry cerco_routes_table[];\n");
  fprintf(f, "extern const unsigned long cerco_routes_count;\n");
  fprintf(f, "extern const cerco_sf_entry cerco_sfs_table[];\n");
  fprintf(f, "extern const unsigned long cerco_sfs_count;\n");
  fprintf(f, "extern const cerco_asset_entry cerco_assets_table[];\n");
  fprintf(f, "extern const unsigned long cerco_assets_count;\n");
  fprintf(f, "extern const cerco_layout_fn cerco_root_layout_sym;\n\n");
  fprintf(f, "int main(int argc, char **argv) {\n");
  fprintf(f, "  cerco_app app;\n");
  fprintf(f, "  app.name = \"%s\";\n", b->proj->name);
  fprintf(f, "  app.routes = cerco_routes_table;\n");
  fprintf(f, "  app.n_routes = cerco_routes_count;\n");
  fprintf(f, "  app.root_layout = cerco_root_layout_sym;\n");
  fprintf(f, "  app.sfs = cerco_sfs_table;\n");
  fprintf(f, "  app.n_sfs = cerco_sfs_count;\n");
  fprintf(f, "  app.assets = cerco_assets_table;\n");
  fprintf(f, "  app.n_assets = cerco_assets_count;\n");
  fprintf(f, "  return cerco_serve(&app, argc, argv);\n");
  fprintf(f, "}\n");
  fclose(f);
  return 0;
}

static const char *ctype_for(char t) {
  switch (t) {
    case 'i': return "int32_t";
    case 'l': return "int64_t";
    case 'f': return "double";
    case 'b': return "uint8_t";
    case 's': return "const char *";
    case 'y': return "cerco_bytes";
    default: return "int32_t";
  }
}

static int gen_sf_server(build_ctx *b) {
  char path[1400];
  snprintf(path, sizeof(path), "%s/sf_server.c", b->gendir);
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "/* generated by cerco — do not edit */\n");
  fprintf(f, "#include \"internal.h\"\n");
  fprintf(f, "#include \"cerco.h\"\n\n");
  for (int i = 0; i < b->n_sfs; i++) {
    sf_info *sf = &b->sfs[i];
    fprintf(f, "extern %s sf_%s(cerco_sf_ctx *ctx", ctype_for(sf->ret), sf->name);
    for (int a = 0; a < sf->n_args; a++) {
      fprintf(f, ", %s a%d", ctype_for(sf->args[a]), a);
    }
    fprintf(f, ");\n");
  }
  fprintf(f, "\n");
  for (int i = 0; i < b->n_sfs; i++) {
    sf_info *sf = &b->sfs[i];
    fprintf(f, "static void sf_call_%u(cerco_req *req, cerco_sf_ctx *ctx) {\n", sf->id);
    fprintf(f, "  (void)req;\n");
    for (int a = 0; a < sf->n_args; a++) {
      fprintf(f, "  cerco_wval v%d;\n", a);
      fprintf(f, "  if (!sf_arg_of_type(ctx, '%c', &v%d)) { ctx->reader.err = 1; return; }\n",
              sf->args[a], a);
    }
    fprintf(f, "  ctx->ret_type = %d;\n",
            sf->ret == 'i' ? 0 : sf->ret == 'l' ? 1 : sf->ret == 'f' ? 2
            : sf->ret == 'b' ? 3 : sf->ret == 's' ? 4 : 5);
    fprintf(f, "  ");
    if (sf->ret != 'y') fprintf(f, "ctx->ret_%s = ",
        sf->ret == 'i' ? "i32" : sf->ret == 'l' ? "i64" : sf->ret == 'f' ? "f64"
        : sf->ret == 'b' ? "bool" : "str");
    else fprintf(f, "ctx->ret_bytes = ");
    fprintf(f, "sf_%s(ctx", sf->name);
    for (int a = 0; a < sf->n_args; a++) {
      fprintf(f, ", ");
      if (sf->args[a] == 's') fprintf(f, "v%d.bytes.data ? (const char *)v%d.bytes.data : \"\"", a, a);
      else if (sf->args[a] == 'y') fprintf(f, "*(cerco_bytes *)&v%d.bytes", a);
      else if (sf->args[a] == 'i') fprintf(f, "v%d.as.i32", a);
      else if (sf->args[a] == 'l') fprintf(f, "v%d.as.i64", a);
      else if (sf->args[a] == 'f') fprintf(f, "v%d.as.f64", a);
      else fprintf(f, "v%d.as.b", a);
    }
    fprintf(f, ");\n}\n");
  }
  fprintf(f, "\nconst cerco_sf_entry cerco_sfs_table[] = {\n");
  for (int i = 0; i < b->n_sfs; i++) {
    sf_info *sf = &b->sfs[i];
    fprintf(f, "  { %u, \"%s\", \"%s\", sf_call_%u },\n", sf->id, sf->method,
            sf->name, sf->id);
  }
  fprintf(f, "};\n");
  fprintf(f, "const unsigned long cerco_sfs_count = %d;\n", b->n_sfs);
  fclose(f);
  return 0;
}

static void gen_sf_proto(FILE *f, sf_info *sf) {
  fprintf(f, "void cerco_sf_%s(", sf->name);
  for (int a = 0; a < sf->n_args; a++) {
    fprintf(f, "%s a%d, ", ctype_for(sf->args[a]), a);
  }
  switch (sf->ret) {
    case 'i': fprintf(f, "cerco_sf_cb_i32 cb, void *user)"); break;
    case 'l': fprintf(f, "cerco_sf_cb_i64 cb, void *user)"); break;
    case 'f': fprintf(f, "cerco_sf_cb_f64 cb, void *user)"); break;
    case 'b': fprintf(f, "cerco_sf_cb_i32 cb, void *user)"); break;
    case 's': fprintf(f, "cerco_sf_cb_str cb, void *user)"); break;
    case 'y': fprintf(f, "cerco_sf_cb_bytes cb, void *user)"); break;
    default: break;
  }
}

static int gen_sf_client(build_ctx *b) {
  char path[1400];
  /* header with declarations (auto-included into wasm compiles) */
  snprintf(path, sizeof(path), "%s/sf_client.h", b->gendir);
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "/* generated by cerco — do not edit */\n");
  fprintf(f, "#include \"cerco_client.h\"\n\n");
  for (int i = 0; i < b->n_sfs; i++) {
    gen_sf_proto(f, &b->sfs[i]);
    fprintf(f, ";\n");
  }
  fclose(f);

  /* implementation */
  snprintf(path, sizeof(path), "%s/sf_client.c", b->gendir);
  f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "/* generated by cerco — do not edit */\n");
  fprintf(f, "#include \"cerco_client.h\"\n");
  fprintf(f, "#include \"sf_client.h\"\n\n");
  for (int i = 0; i < b->n_sfs; i++) {
    sf_info *sf = &b->sfs[i];
    gen_sf_proto(f, sf);
    fprintf(f, " {\n");
    fprintf(f, "  cerco_sf_begin(%u, %d);\n", sf->id, sf->n_args);
    for (int a = 0; a < sf->n_args; a++) {
      switch (sf->args[a]) {
        case 'i': fprintf(f, "  cerco_sf_arg_i32(a%d);\n", a); break;
        case 'l': fprintf(f, "  cerco_sf_arg_i64(a%d);\n", a); break;
        case 'f': fprintf(f, "  cerco_sf_arg_f64(a%d);\n", a); break;
        case 'b': fprintf(f, "  cerco_sf_arg_bool(a%d);\n", a); break;
        case 's': fprintf(f, "  cerco_sf_arg_str(a%d);\n", a); break;
        case 'y': fprintf(f, "  cerco_sf_arg_bytes(a%d.data, (int32_t)a%d.len);\n", a, a); break;
      }
    }
    switch (sf->ret) {
      case 'i': fprintf(f, "  cerco_sf_submit_i32((void *)cb, user);\n}\n\n"); break;
      case 'b': fprintf(f, "  cerco_sf_submit_bool((void *)cb, user);\n}\n\n"); break;
      case 'l': fprintf(f, "  cerco_sf_submit_i64((void *)cb, user);\n}\n\n"); break;
      case 'f': fprintf(f, "  cerco_sf_submit_f64((void *)cb, user);\n}\n\n"); break;
      case 's': fprintf(f, "  cerco_sf_submit_str((void *)cb, user);\n}\n\n"); break;
      case 'y': fprintf(f, "  cerco_sf_submit_bytes((void *)cb, user);\n}\n\n"); break;
    }
  }
  fclose(f);
  return 0;
}

static int gen_components(build_ctx *b) {
  char path[1400];
  snprintf(path, sizeof(path), "%s/components.c", b->gendir);
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "/* generated by cerco — do not edit */\n");
  fprintf(f, "#include \"cerco_client.h\"\n\n");
  for (int i = 0; i < b->n_components; i++) {
    fprintf(f, "extern void cerco_component_%s(int32_t root, const char *props);\n",
            b->components[i]);
  }
  fprintf(f, "\nconst cerco_component cerco_components_table[] = {\n");
  for (int i = 0; i < b->n_components; i++) {
    fprintf(f, "  { \"%s\", cerco_component_%s },\n", b->components[i],
            b->components[i]);
  }
  fprintf(f, "};\n");
  fprintf(f, "const uint32_t cerco_components_count = %d;\n", b->n_components);
  fclose(f);
  return 0;
}

/* --------------------------------------------------------------- compile */

static int needs_rebuild(const char *src, const char *obj) {
  int64_t smt = 0, omt = 0;
  if (mtime_ms(src, &smt) != 0) return 1;
  if (mtime_ms(obj, &omt) != 0) return 1;
  return smt > omt;
}

/* find a clang able to target wasm32: CERCO_CLANG > llvm homes > PATH */
static const char *find_clang(void) {
  static char path[512];
  const char *e = getenv("CERCO_CLANG");
  if (e && e[0]) return e;
  static const char *candidates[] = {
    "/opt/homebrew/opt/llvm/bin/clang",
    "/usr/local/opt/llvm/bin/clang",
    NULL,
  };
  for (int i = 0; candidates[i]; i++) {
    if (file_exists(candidates[i])) return candidates[i];
  }
  snprintf(path, sizeof(path), "clang");
  return path;
}

static int compile_obj(build_ctx *b, const char *src_full, const char *obj_rel,
                       const char *extra_define, int wasm, char *out_err,
                       size_t err_cap) {
  char obj_full[1500];
  snprintf(obj_full, sizeof(obj_full), "%s/%s", b->objdir, obj_rel);
  char objdir_only[1500];
  snprintf(objdir_only, sizeof(objdir_only), "%s", obj_full);
  char *slash = strrchr(objdir_only, '/');
  if (slash) *slash = 0;
  if (mkdirs(objdir_only) != 0) return -1;

  if (!needs_rebuild(src_full, obj_full)) return 0;
  if (mkdir_for_file(obj_full) != 0) return -1;

  static char inc_sdk[1300], inc_runtime_server[1300], inc_shared[1300],
      inc_llhttp[1300], inc_libuv[1300];
  snprintf(inc_sdk, sizeof(inc_sdk), "-I%s/include", b->sdk);
  snprintf(inc_runtime_server, sizeof(inc_runtime_server), "-I%s/runtime/server",
           b->sdk);
  snprintf(inc_shared, sizeof(inc_shared), "-I%s/runtime/shared", b->sdk);
  snprintf(inc_llhttp, sizeof(inc_llhttp), "-I%s/vendor/llhttp/include", b->sdk);
  snprintf(inc_libuv, sizeof(inc_libuv), "-I%s/vendor/libuv/include", b->sdk);

  char *argv[28];
  int n = 0;
  argv[n++] = (char *)find_clang();
  argv[n++] = "-c";
  argv[n++] = "-std=c11";
  if (wasm) {
    argv[n++] = "--target=wasm32";
    argv[n++] = "-Oz";
    argv[n++] = "-DCERCO_CLIENT";
  } else {
    argv[n++] = b->release ? "-O2" : "-O1";
    argv[n++] = b->release ? "-g0" : "-g";
#if defined(__APPLE__)
#else
    argv[n++] = "-D_GNU_SOURCE";
#endif
  }
  argv[n++] = "-fno-strict-aliasing";
  argv[n++] = "-Wall";
  argv[n++] = inc_sdk;
  {
    static char inc_src[1200], inc_shared[1200];
    snprintf(inc_src, sizeof(inc_src), "-I%s/src", b->proj->root);
    snprintf(inc_shared, sizeof(inc_shared), "-I%s/src/shared", b->proj->root);
    argv[n++] = inc_src;
    argv[n++] = inc_shared;
  }
  if (!wasm) {
    argv[n++] = inc_runtime_server;
    argv[n++] = inc_shared;
    argv[n++] = inc_llhttp;
    argv[n++] = inc_libuv;
  } else {
    /* auto-include generated server function stubs for client code */
    static char inc_gen[1300], inc_sf[1300];
    snprintf(inc_gen, sizeof(inc_gen), "-I%s", b->gendir);
    snprintf(inc_sf, sizeof(inc_sf), "-include%s/sf_client.h", b->gendir);
    argv[n++] = inc_gen;
    argv[n++] = inc_sf;
  }
  argv[n++] = (char *)src_full;
  argv[n++] = "-o";
  argv[n++] = obj_full;
  if (extra_define) argv[n++] = (char *)extra_define;
  argv[n] = NULL;

  char *captured = NULL;
  size_t clen = 0;
  int rc = run_cmd(argv, b->proj->root, 1, &captured, &clen);
  if (rc != 0) {
    snprintf(out_err, err_cap, "%s", captured ? captured : "compile failed");
    free(captured);
    return -1;
  }
  free(captured);
  return 0;
}

/* define buffer helper: "-DNAME=value" */
static void make_define(char *buf, size_t cap, const char *name, const char *value) {
  snprintf(buf, cap, "-D%s=%s", name, value);
}

typedef struct {
  build_ctx *b;
  char err[8192];
  int err_len;
  int wasm; /* current pass: 1 = wasm sources, 0 = server sources */
  char objprefix[32];
} walk_ctx;

static void compile_app_sources_cb(const char *rel, const char *full, int is_dir,
                                   void *user) {
  walk_ctx *w = (walk_ctx *)user;
  build_ctx *b = w->b;
  if (is_dir) return;
  size_t rl = strlen(rel);
  if (rl < 2 || strcmp(rel + rl - 2, ".c") != 0) return;

  if (w->wasm) {
    /* client: src/components + src/shared + src/client ; NEVER src/server or
     * src/routes (server-only) */
    if (strncmp(rel, "src/server/", 11) == 0) return;
    if (strncmp(rel, "src/routes/", 11) == 0) return;
    if (strncmp(rel, "src/components/", 15) != 0 &&
        strncmp(rel, "src/shared/", 11) != 0 &&
        strncmp(rel, "src/client/", 11) != 0)
      return;
  } else {
    /* server: src/routes + src/server + src/shared */
    if (strncmp(rel, "src/routes/", 11) != 0 &&
        strncmp(rel, "src/server/", 11) != 0 &&
        strncmp(rel, "src/shared/", 11) != 0)
      return;
  }

  char mg[560];
  mangle(rel, mg, sizeof(mg));
  char obj_rel[600];
  snprintf(obj_rel, sizeof(obj_rel), "%s_%s.o", w->objprefix, mg);

  char extra[256] = {0};
  /* route/layout files get unique symbols */
  if (!w->wasm && strncmp(rel, "src/routes/", 11) == 0) {
    const char *base = path_basename(rel);
    if (!strcmp(base, "layout.c")) {
      for (int i = 0; i < b->n_layouts; i++) {
        char want[512];
        snprintf(want, sizeof(want), "src/routes/%s", b->layouts[i].dir);
        size_t wl = strlen(want);
        int match = strncmp(rel, want, wl) == 0 &&
                    (strcmp(rel + wl, "layout.c") == 0 ||
                     strcmp(rel + wl, "/layout.c") == 0);
        if (match) {
          make_define(extra, sizeof(extra), "CERCO_LAYOUT_SYMBOL",
                      b->layouts[i].symbol);
          break;
        }
      }
    } else {
      for (int i = 0; i < b->n_routes; i++) {
        if (!strcmp(b->routes[i].rel, rel)) {
          make_define(extra, sizeof(extra), "CERCO_ROUTE_SYMBOL",
                      b->routes[i].symbol);
          break;
        }
      }
    }
  }

  char err[4096];
  if (compile_obj(b, full, obj_rel, extra[0] ? extra : NULL, w->wasm, err,
                  sizeof(err)) != 0) {
    if (w->err_len < (int)sizeof(w->err) - 1) {
      w->err_len += snprintf(w->err + w->err_len,
                             sizeof(w->err) - (size_t)w->err_len, "%s\n", err);
    }
  }
}

/* compile sdk runtime sources (native server side) */
static void compile_sdk_sources_cb(const char *rel, const char *full, int is_dir,
                                   void *user) {
  walk_ctx *w = (walk_ctx *)user;
  build_ctx *b = w->b;
  if (is_dir) return;
  size_t rl = strlen(rel);
  if (rl < 2 || strcmp(rel + rl - 2, ".c") != 0) return;
  if (strncmp(rel, "runtime/client/", 15) == 0) return; /* wasm only */
  if (strncmp(rel, "runtime/browser/", 16) == 0) return; /* js */
  if (strncmp(rel, "runtime/server/", 15) != 0 &&
      strncmp(rel, "runtime/shared/", 15) != 0)
    return;

  char mg[560];
  mangle(rel, mg, sizeof(mg));
  char obj_rel2[620];
  snprintf(obj_rel2, sizeof(obj_rel2), "sdk_%s.o", mg);
  char err[4096];
  if (compile_obj(b, full, obj_rel2, NULL, w->wasm, err, sizeof(err)) != 0) {
    if (w->err_len < (int)sizeof(w->err) - 1) {
      w->err_len += snprintf(w->err + w->err_len,
                             sizeof(w->err) - (size_t)w->err_len, "%s\n", err);
    }
  }
}

static void compile_wasm_runtime_cb(const char *rel, const char *full, int is_dir,
                                    void *user) {
  walk_ctx *w = (walk_ctx *)user;
  build_ctx *b = w->b;
  if (is_dir) return;
  if (strncmp(rel, "runtime/client/", 15) != 0) return;
  size_t rl = strlen(rel);
  if (rl < 2 || strcmp(rel + rl - 2, ".c") != 0) return;
  char mg[560];
  mangle(rel, mg, sizeof(mg));
  char obj_rel2[620];
  snprintf(obj_rel2, sizeof(obj_rel2), "wrt_%s.o", mg);
  char err[4096];
  if (compile_obj(b, full, obj_rel2, NULL, 1, err, sizeof(err)) != 0) {
    if (w->err_len < (int)sizeof(w->err) - 1) {
      w->err_len += snprintf(w->err + w->err_len,
                             sizeof(w->err) - (size_t)w->err_len, "%s\n", err);
    }
  }
}

/* collect object file paths by re-walking (objects were produced above) */
typedef struct {
  build_ctx *b;
  cerco_sb *list;
  const char *prefix;
} collect_ctx;

static void collect_obj_cb(const char *rel, const char *full, int is_dir,
                           void *user) {
  (void)rel;
  collect_ctx *c = (collect_ctx *)user;
  if (is_dir) return;
  size_t l = strlen(full);
  if (l < 2 || strcmp(full + l - 2, ".o") != 0) return;
  if (strncmp(path_basename(full), c->prefix, strlen(c->prefix)) != 0) return;
  sb_printf(c->list, "%s ", full);
}

/* generate embedded assets table from dist/ */
typedef struct {
  char rel[512];
  char etag[64];
  unsigned long len;
} asset_meta;

typedef struct {
  build_ctx *b;
  FILE *f;
  int n;
  unsigned long total;
  asset_meta meta[128];
} asset_ctx;

static void gen_assets_cb(const char *rel, const char *full, int is_dir, void *user) {
  asset_ctx *a = (asset_ctx *)user;
  if (is_dir) return;
  /* never embed the output binary itself */
  if (strcmp(rel, a->b->proj->name) == 0) return;
  if (a->n >= 128) return;
  size_t len = 0;
  char *data = read_file(full, &len);
  if (!data) return;
  uint8_t digest[32];
  char etag[65];
  cerco_sha256(data, len, digest);
  static const char hexd[] = "0123456789abcdef";
  for (int i = 0; i < 16; i++) {
    etag[i * 2] = hexd[digest[i] >> 4];
    etag[i * 2 + 1] = hexd[digest[i] & 0xf];
  }
  etag[32] = 0;
  fprintf(a->f, "static const unsigned char asset_bytes_%d[] = {\n", a->n);
  for (size_t i = 0; i < len; i++) {
    fprintf(a->f, "%u,", (unsigned)(unsigned char)data[i]);
    if ((i + 1) % 24 == 0) fprintf(a->f, "\n");
  }
  fprintf(a->f, "\n};\n");
  snprintf(a->meta[a->n].rel, sizeof(a->meta[a->n].rel), "%s", rel);
  snprintf(a->meta[a->n].etag, sizeof(a->meta[a->n].etag), "%s", etag);
  a->meta[a->n].len = (unsigned long)len;
  a->n++;
  a->total += (unsigned long)len;
  free(data);
}

static int gen_assets(build_ctx *b) {
  char path[1400];
  snprintf(path, sizeof(path), "%s/assets.c", b->gendir);
  FILE *f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "/* generated by cerco — do not edit */\n");
  fprintf(f, "#include \"cerco.h\"\n\n");
  fprintf(f, "#include <stddef.h>\n\n");
  if (!b->dev_assets) {
    asset_ctx a = { b, f, 0, 0 };
    walk_dir(b->distdir, "", gen_assets_cb, &a);
    fprintf(f, "\nconst cerco_asset_entry cerco_assets_table[] = {\n");
    for (int i = 0; i < a.n; i++) {
      fprintf(f, "  { \"/%s\", \"%s\", \"%s\", asset_bytes_%d, %lu },\n",
              a.meta[i].rel, cerco_mime_from_ext(a.meta[i].rel),
              a.meta[i].etag, i, a.meta[i].len);
    }
    fprintf(f, "};\n");
    fprintf(f, "const unsigned long cerco_assets_count = %d;\n", a.n);
    if (g_verbose) printf("  assets: %d files, %lu bytes embedded\n", a.n, a.total);
  } else {
    fprintf(f, "const cerco_asset_entry cerco_assets_table[] = { {0,0,0,0,0} };\n");
    fprintf(f, "const unsigned long cerco_assets_count = 0;\n");
  }
  fclose(f);
  return 0;
}

static int link_server(build_ctx *b, cerco_sb *objs, char *out_err, size_t err_cap) {
  char bin[1400];
  snprintf(bin, sizeof(bin), "%s/%s", b->distdir, b->proj->name);
  char llhttp[1300], libuv[1300];
  snprintf(llhttp, sizeof(llhttp), "%s/lib/libllhttp.a", b->sdk);
  snprintf(libuv, sizeof(libuv), "%s/lib/libuv.a", b->sdk);

  /* count object files, then build argv with matching lifetime */
  int n_objs = 0;
  {
    char *dup = strdup(objs->buf ? objs->buf : "");
    char *tok = strtok(dup, " ");
    while (tok) { n_objs++; tok = strtok(NULL, " "); }
    free(dup);
  }
  char **argv = (char **)calloc((size_t)(n_objs + 24), sizeof(char *));
  int n = 0;
  argv[n++] = (char *)find_clang();
  argv[n++] = (char *)(b->release ? "-O2" : "-O1");
  argv[n++] = "-o";
  argv[n++] = bin;
#if defined(__APPLE__)
  argv[n++] = "-Wl,-dead_strip";
  argv[n++] = "-framework";
  argv[n++] = "CoreFoundation";
  argv[n++] = "-framework";
  argv[n++] = "CoreServices";
#else
  argv[n++] = "-Wl,--gc-sections";
  argv[n++] = "-ldl";
#endif
  char *obj_dup = strdup(objs->buf ? objs->buf : "");
  {
    char *tok = strtok(obj_dup, " ");
    while (tok) { argv[n++] = tok; tok = strtok(NULL, " "); }
  }
  argv[n++] = llhttp;
  argv[n++] = libuv;
  argv[n++] = "-lm";
#if !defined(__APPLE__)
  argv[n++] = "-pthread";
  argv[n++] = "-ldl";
#endif
  argv[n] = NULL;
  char *captured = NULL;
  size_t clen = 0;
  int rc = run_cmd(argv, b->proj->root, 1, &captured, &clen);
  free(obj_dup);
  free(argv);
  if (rc != 0) {
    snprintf(out_err, err_cap, "%s", captured ? captured : "link failed");
    free(captured);
    return -1;
  }
  free(captured);
  chmod(bin, 0755);
  return 0;
}

static int link_wasm(build_ctx *b, cerco_sb *objs, char *out_err, size_t err_cap) {
  char out[1400];
  snprintf(out, sizeof(out), "%s/assets/app.wasm", b->distdir);
  mkdir_for_file(out);

  int n_objs = 0;
  {
    char *dup = strdup(objs->buf ? objs->buf : "");
    char *tok = strtok(dup, " ");
    while (tok) { n_objs++; tok = strtok(NULL, " "); }
    free(dup);
  }
  char **argv = (char **)calloc((size_t)(n_objs + 16), sizeof(char *));
  int n = 0;
  argv[n++] = (char *)find_clang();
  argv[n++] = "--target=wasm32";
  argv[n++] = "-nostdlib";
  argv[n++] = "-Wl,--no-entry";
  argv[n++] = "-Wl,--export-dynamic";
  argv[n++] = "-Wl,--gc-sections";
  if (b->release) argv[n++] = "-Wl,--strip-all";
  argv[n++] = "-o";
  argv[n++] = out;
  char *obj_dup = strdup(objs->buf ? objs->buf : "");
  {
    char *tok = strtok(obj_dup, " ");
    while (tok) { argv[n++] = tok; tok = strtok(NULL, " "); }
  }
  argv[n] = NULL;
  char *captured = NULL;
  size_t clen = 0;
  int rc = run_cmd(argv, b->proj->root, 1, &captured, &clen);
  free(obj_dup);
  free(argv);
  if (rc != 0) {
    snprintf(out_err, err_cap, "%s", captured ? captured : "wasm link failed");
    free(captured);
    return -1;
  }
  free(captured);
  return 0;
}

static void copy_public_cb(const char *rel, const char *full, int is_dir, void *user) {
  build_ctx *b = (build_ctx *)user;
  if (is_dir) return;
  if (strncmp(rel, "public/", 7) != 0) return;
  char dest[1400];
  snprintf(dest, sizeof(dest), "%s/%s", b->distdir, rel + 7);
  if (mkdir_for_file(dest) != 0) return;
  size_t len = 0;
  char *data = read_file(full, &len);
  if (!data) return;
  write_file(dest, data, len);
  free(data);
}

static void audit_client_sources(build_ctx *b, char *out_err, size_t err_cap) {
  /* fail the build if server-only code would end up in the wasm target */
  for (int i = 0; i < b->n_routes; i++) {
    char full[1400];
    snprintf(full, sizeof(full), "%s/%s", b->proj->root, b->routes[i].rel);
    size_t len = 0;
    char *data = read_file(full, &len);
    if (!data) continue;
    if (strstr(data, "cerco_client.h")) {
      snprintf(out_err, err_cap,
               "%s: route files are server-only and must not include cerco_client.h\n",
               b->routes[i].rel);
      free(data);
      exit(1);
    }
    free(data);
  }
}

/* --------------------------------------------------------------- cmd_build */

int cmd_build(cerco_project *proj, int argc, char **argv) {
  int debug = 0;
  build_ctx b;
  memset(&b, 0, sizeof(b));
  b.proj = proj;
  for (int i = 0; i < argc; i++) {
    if (!strcmp(argv[i], "--debug")) debug = 1;
    else if (!strcmp(argv[i], "--release")) debug = 0;
    else if (!strcmp(argv[i], "--dev-assets")) b.dev_assets = 1;
  }
  b.release = !debug;
  b.dev_mode = 0;
  snprintf(b.gendir, sizeof(b.gendir), "%s/.cerco/gen", proj->root);
  snprintf(b.objdir, sizeof(b.objdir), "%s/.cerco/obj", proj->root);
  snprintf(b.distdir, sizeof(b.distdir), "%s/dist", proj->root);

  if (sdk_ensure(proj, b.sdk, sizeof(b.sdk)) != 0) die("sdk extraction failed");
  if (mkdirs(b.gendir) != 0) die("mkdir gen failed");
  if (mkdirs(b.distdir) != 0) die("mkdir dist failed");

  /* scan routes */
  scan_ctx s = { &b, "" };
  snprintf(s.routes_root, sizeof(s.routes_root), "%s/src/routes", proj->root);
  if (!is_dir(s.routes_root)) die("missing src/routes/ directory");
  walk_dir(s.routes_root, "", scan_cb, &s);
  compute_layout_chains(&b);

  if (scan_sf(&b) != 0) die("failed to parse server functions");
  b.n_components = 0;
  walk_dir(proj->root, "", scan_components_cb, &b);

  audit_client_sources(&b, (char[1]){0}, 1);

  if (g_verbose)
    printf("routes %d, layouts %d, server fns %d, components %d\n", b.n_routes,
           b.n_layouts, b.n_sfs, b.n_components);

  if (gen_routes(&b) != 0) die("routes codegen failed");
  if (gen_main(&b) != 0) die("main codegen failed");
  if (gen_sf_server(&b) != 0) die("sf server codegen failed");
  if (gen_sf_client(&b) != 0) die("sf client codegen failed");
  if (gen_components(&b) != 0) die("components codegen failed");

  char err[8192];
  /* ---- wasm target ---- */
  cerco_sb wasm_objs;
  sb_init(&wasm_objs);
  walk_ctx ww = { &b, {0}, 0, 1, "wa" };
  walk_dir(proj->root, "", compile_app_sources_cb, &ww);
  walk_dir(b.sdk, "", compile_wasm_runtime_cb, &ww);
  {
    char f[1400];
    char orl[128];
    snprintf(f, sizeof(f), "%s/sf_client.c", b.gendir);
    snprintf(orl, sizeof(orl), "wa_gen_sf_client.o");
    char e2[4096];
    if (compile_obj(&b, f, orl, NULL, 1, e2, sizeof(e2)) != 0) {
      fprintf(stderr, "%s\n", e2);
      return 1;
    }
    snprintf(f, sizeof(f), "%s/components.c", b.gendir);
    snprintf(orl, sizeof(orl), "wa_gen_components.o");
    if (compile_obj(&b, f, orl, NULL, 1, e2, sizeof(e2)) != 0) {
      fprintf(stderr, "%s\n", e2);
      return 1;
    }
  }
  if (ww.err_len) {
    fprintf(stderr, "wasm build failed:\n%s\n", ww.err);
    return 1;
  }
  collect_ctx cw = { &b, &wasm_objs, "wa_" };
  walk_dir(b.objdir, "", collect_obj_cb, &cw);
  collect_ctx cw2 = { &b, &wasm_objs, "wrt_" };
  walk_dir(b.objdir, "", collect_obj_cb, &cw2);
  if (link_wasm(&b, &wasm_objs, err, sizeof(err)) != 0) {
    fprintf(stderr, "wasm link failed:\n%s\n", err);
    return 1;
  }
  sb_free(&wasm_objs);

  /* ---- host.js + public ---- */
  {
    char src[1400], dst[1400];
    snprintf(src, sizeof(src), "%s/runtime/browser/host.js", b.sdk);
    snprintf(dst, sizeof(dst), "%s/assets/host.js", b.distdir);
    mkdir_for_file(dst);
    size_t len = 0;
    char *data = read_file(src, &len);
    if (data) { write_file(dst, data, len); free(data); }
    else fprintf(stderr, "cerco: warning: host.js missing\n");
  }
  {
    char pub[1300];
    snprintf(pub, sizeof(pub), "%s/public", proj->root);
    if (is_dir(pub)) walk_dir(proj->root, "", copy_public_cb, &b);
  }

  /* ---- css (tailwind) ---- */
  char twbin[1200];
  char css_in[1400];
  snprintf(css_in, sizeof(css_in), "%s/src/styles.css", proj->root);
  if (file_exists(css_in) && tailwind_ensure(proj, twbin, sizeof(twbin)) == 0) {
    char css_out[1400];
    snprintf(css_out, sizeof(css_out), "%s/assets/styles.css", b.distdir);
    mkdir_for_file(css_out);
    if (tailwind_run(proj, twbin, proj->root, "src/styles.css",
                     "dist/assets/styles.css", 0) != 0) {
      fprintf(stderr, "cerco: warning: tailwind build failed; continuing without css\n");
    }
  }

  /* ---- native server ---- */
  cerco_sb srv_objs;
  sb_init(&srv_objs);
  walk_ctx sw = { &b, {0}, 0, 0, "sa" };
  walk_dir(proj->root, "", compile_app_sources_cb, &sw);
  walk_dir(b.sdk, "", compile_sdk_sources_cb, &sw);
  {
    char f[1400];
    char orl[128];
    char e2[4096];
    snprintf(f, sizeof(f), "%s/routes.c", b.gendir);
    snprintf(orl, sizeof(orl), "sa_gen_routes.o");
    if (compile_obj(&b, f, orl, NULL, 0, e2, sizeof(e2)) != 0) {
      fprintf(stderr, "%s\n", e2);
      return 1;
    }
    snprintf(f, sizeof(f), "%s/main.c", b.gendir);
    snprintf(orl, sizeof(orl), "sa_gen_main.o");
    if (compile_obj(&b, f, orl, NULL, 0, e2, sizeof(e2)) != 0) {
      fprintf(stderr, "%s\n", e2);
      return 1;
    }
    snprintf(f, sizeof(f), "%s/sf_server.c", b.gendir);
    snprintf(orl, sizeof(orl), "sa_gen_sf_server.o");
    if (compile_obj(&b, f, orl, NULL, 0, e2, sizeof(e2)) != 0) {
      fprintf(stderr, "%s\n", e2);
      return 1;
    }
  }
  if (sw.err_len) {
    fprintf(stderr, "server build failed:\n%s\n", sw.err);
    return 1;
  }
  if (gen_assets(&b) != 0) die("assets codegen failed");
  {
    char f[1400], orl[128], e2[4096];
    snprintf(f, sizeof(f), "%s/assets.c", b.gendir);
    snprintf(orl, sizeof(orl), "sa_gen_assets.o");
    if (compile_obj(&b, f, orl, NULL, 0, e2, sizeof(e2)) != 0) {
      fprintf(stderr, "%s\n", e2);
      return 1;
    }
  }

  collect_ctx cs = { &b, &srv_objs, "sa_" };
  walk_dir(b.objdir, "", collect_obj_cb, &cs);
  collect_ctx cs2 = { &b, &srv_objs, "sdk_" };
  walk_dir(b.objdir, "", collect_obj_cb, &cs2);

  if (link_server(&b, &srv_objs, err, sizeof(err)) != 0) {
    fprintf(stderr, "server link failed:\n%s\n", err);
    return 1;
  }
  sb_free(&srv_objs);

  /* sizes report */
  struct {
    const char *label;
    char path[1400];
  } sizes[5];
  snprintf(sizes[0].path, sizeof(sizes[0].path), "%s/%s", b.distdir, proj->name);
  sizes[0].label = "binary";
  snprintf(sizes[1].path, sizeof(sizes[1].path), "%s/assets/app.wasm", b.distdir);
  sizes[1].label = "wasm";
  snprintf(sizes[2].path, sizeof(sizes[2].path), "%s/assets/host.js", b.distdir);
  sizes[2].label = "js host";
  snprintf(sizes[3].path, sizeof(sizes[3].path), "%s/assets/styles.css", b.distdir);
  sizes[3].label = "css";
  snprintf(sizes[4].path, sizeof(sizes[4].path), "%s", "");
  sizes[4].label = NULL;

  printf("build ok (%s)\n", b.release ? "release" : "debug");
  for (int i = 0; i < 4; i++) {
    struct stat st;
    if (stat(sizes[i].path, &st) == 0) {
      printf("  %-8s %8ld  %s\n", sizes[i].label, (long)st.st_size,
             sizes[i].path + strlen(proj->root) + 1);
    }
  }
  return 0;
}

