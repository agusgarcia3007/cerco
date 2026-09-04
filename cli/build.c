#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1 /* sysctlbyname + BSD types alongside POSIX */
#endif
#include "main.h"
#include "util.h"
#include "bundle.h"
#include "sha256.h"
#include "str.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

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
  char sdk_hash[33];   /* content hash of the extracted sdk bundle */
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
  if (!strcmp(basebuf, "404")) {
    /* custom not-found page: registered under the framework-internal path */
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
  /* dedupe symbols: a dir may hold index.c + index.post.c + ... which all
   * mangle to the same name; suffix _1, _2, ... until unique */
  char symbase[160];
  snprintf(symbase, sizeof(symbase), "cerco_route_%s", mg);
  for (int suffix = 0;; suffix++) {
    if (suffix)
      snprintf(r->symbol, sizeof(r->symbol), "%s_%d", symbase, suffix);
    else
      snprintf(r->symbol, sizeof(r->symbol), "%s", symbase);
    int dup = 0;
    for (int i = 0; i < b->n_routes; i++) {
      if (!strcmp(b->routes[i].symbol, r->symbol)) {
        dup = 1;
        break;
      }
    }
    if (!dup) break;
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
    /* walk ancestors from the route dir up to and including the root ("") */
    char dirs[MAX_LAYOUTS][256];
    int nd = 0;
    char work[256];
    snprintf(work, sizeof(work), "%s", r->dir);
    for (;;) {
      if (nd >= MAX_LAYOUTS) break;
      snprintf(dirs[nd++], 256, "%s", work);
      char *slash = strrchr(work, '/');
      if (!slash) {
        /* no more slashes: after the dir itself, include the root */
        if (work[0] && nd < MAX_LAYOUTS) snprintf(dirs[nd++], 256, "");
        break;
      }
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

/* write only when the content differs: keeps mtimes stable so unchanged
 * generated sources neither recompile nor force relinks on no-op builds */
static int write_if_changed(const char *path, const char *data, size_t len) {
  size_t elen = 0;
  char *existing = read_file(path, &elen);
  if (existing && elen == len && memcmp(existing, data, len) == 0) {
    free(existing);
    return 0;
  }
  free(existing);
  return write_file(path, data, len);
}

static int gen_routes(build_ctx *b) {
  char path[1400];
  snprintf(path, sizeof(path), "%s/routes.c", b->gendir);
  char *buf = NULL;
  size_t blen = 0;
  FILE *f = open_memstream(&buf, &blen);
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
  int rc = write_if_changed(path, buf, blen) == 0 ? 0 : -1;
  free(buf);
  return rc;
}

static int gen_main(build_ctx *b) {
  char path[1400];
  snprintf(path, sizeof(path), "%s/main.c", b->gendir);
  char *buf = NULL;
  size_t blen = 0;
  FILE *f = open_memstream(&buf, &blen);
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
  int rc = write_if_changed(path, buf, blen) == 0 ? 0 : -1;
  free(buf);
  return rc;
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
  char *buf = NULL;
  size_t blen = 0;
  FILE *f = open_memstream(&buf, &blen);
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
  int rc = write_if_changed(path, buf, blen) == 0 ? 0 : -1;
  free(buf);
  return rc;
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
  char *buf = NULL;
  size_t blen = 0;
  /* header with declarations (auto-included into wasm compiles) */
  snprintf(path, sizeof(path), "%s/sf_client.h", b->gendir);
  FILE *f = open_memstream(&buf, &blen);
  if (!f) return -1;
  fprintf(f, "/* generated by cerco — do not edit */\n");
  fprintf(f, "#include \"cerco_client.h\"\n\n");
  for (int i = 0; i < b->n_sfs; i++) {
    gen_sf_proto(f, &b->sfs[i]);
    fprintf(f, ";\n");
  }
  fclose(f);
  if (write_if_changed(path, buf, blen) != 0) {
    free(buf);
    return -1;
  }
  free(buf);

  /* implementation */
  snprintf(path, sizeof(path), "%s/sf_client.c", b->gendir);
  f = open_memstream(&buf, &blen);
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
  int rc = write_if_changed(path, buf, blen) == 0 ? 0 : -1;
  free(buf);
  return rc;
}

static int gen_components(build_ctx *b) {
  char path[1400];
  snprintf(path, sizeof(path), "%s/components.c", b->gendir);
  char *buf = NULL;
  size_t blen = 0;
  FILE *f = open_memstream(&buf, &blen);
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
  int rc = write_if_changed(path, buf, blen) == 0 ? 0 : -1;
  free(buf);
  return rc;
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

/* run one clang -c invocation; thread-safe (no shared mutable state) */
static int run_clang_compile(build_ctx *b, const char *src_full,
                             const char *out_obj, const char *extra_define,
                             int wasm, char *out_err, size_t err_cap) {
  char inc_sdk[1300], inc_runtime_server[1300], inc_shared[1300],
      inc_llhttp[1300], inc_libuv[1300], inc_src[1200], inc_shared2[1200],
      inc_gen[1300], inc_sf[1300];
  snprintf(inc_sdk, sizeof(inc_sdk), "-I%s/include", b->sdk);
  snprintf(inc_runtime_server, sizeof(inc_runtime_server), "-I%s/runtime/server",
           b->sdk);
  snprintf(inc_shared, sizeof(inc_shared), "-I%s/runtime/shared", b->sdk);
  snprintf(inc_llhttp, sizeof(inc_llhttp), "-I%s/vendor/llhttp/include", b->sdk);
  snprintf(inc_libuv, sizeof(inc_libuv), "-I%s/vendor/libuv/include", b->sdk);
  snprintf(inc_src, sizeof(inc_src), "-I%s/src", b->proj->root);
  snprintf(inc_shared2, sizeof(inc_shared2), "-I%s/src/shared", b->proj->root);
  snprintf(inc_gen, sizeof(inc_gen), "-I%s", b->gendir);
  snprintf(inc_sf, sizeof(inc_sf), "-include%s/sf_client.h", b->gendir);

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
  argv[n++] = inc_src;
  argv[n++] = inc_shared2;
  if (!wasm) {
    argv[n++] = inc_runtime_server;
    argv[n++] = inc_shared;
    argv[n++] = inc_llhttp;
    argv[n++] = inc_libuv;
  } else {
    /* auto-include generated server function stubs for client code */
    argv[n++] = inc_gen;
    argv[n++] = inc_sf;
  }
  argv[n++] = (char *)src_full;
  argv[n++] = "-o";
  argv[n++] = (char *)out_obj;
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

/* materialize a cached object into the project objdir (hardlink, fall back
 * to copy across filesystems). Always refreshes: cache content is
 * authoritative for sdk sources. */
static int install_cached_obj(const char *cached, const char *obj_full) {
  unlink(obj_full);
  if (link(cached, obj_full) == 0) return 0;
  size_t len = 0;
  char *data = read_file(cached, &len);
  if (!data) return -1;
  int rc = write_file(obj_full, data, len);
  free(data);
  return rc == 0 ? 0 : -1;
}

/* cache dir for sdk objects: shared across every project built by this
 * machine; keyed by the sdk compile-inputs hash (see sdk.c) + target + mode,
 * so template/host.js edits never invalidate compiled runtime objects */
static int sdk_cache_path(build_ctx *b, int wasm, const char *obj_rel,
                          char *out, size_t cap) {
  if (!b->sdk_hash[0]) return -1;
  const char *home = getenv("CERCO_CACHE_DIR");
  if (!home || !home[0]) home = getenv("HOME");
  if (!home || !home[0]) return -1;
  const char *mode = wasm ? "wasm" : (b->release ? "native-release" : "native-debug");
  snprintf(out, cap, "%s/.cerco/cache/%s/%s/%s", home, b->sdk_hash, mode,
           obj_rel);
  return 0;
}

static int compile_obj(build_ctx *b, const char *src_full, const char *obj_rel,
                       const char *extra_define, int wasm, int from_sdk,
                       char *out_err, size_t err_cap) {
  char obj_full[1500];
  snprintf(obj_full, sizeof(obj_full), "%s/%s", b->objdir, obj_rel);
  char objdir_only[1500];
  snprintf(objdir_only, sizeof(objdir_only), "%s", obj_full);
  char *slash = strrchr(objdir_only, '/');
  if (slash) *slash = 0;
  if (mkdirs(objdir_only) != 0) return -1;

  if (!from_sdk && !needs_rebuild(src_full, obj_full)) return 0;

  /* sdk sources are identical across projects: compile once per cerco
   * build into a global cache, then hardlink into the project */
  char cached[1600];
  if (from_sdk && sdk_cache_path(b, wasm, obj_rel, cached, sizeof(cached)) == 0) {
    if (file_exists(cached)) {
      if (install_cached_obj(cached, obj_full) == 0) return 0;
    } else {
      if (mkdir_for_file(cached) != 0) {
        snprintf(out_err, err_cap, "cache mkdir failed: %s", cached);
        return -1;
      }
      char tmp[1700];
      snprintf(tmp, sizeof(tmp), "%s.tmp%d", cached, (int)getpid());
      if (run_clang_compile(b, src_full, tmp, extra_define, wasm, out_err,
                            err_cap) == 0 &&
          rename(tmp, cached) == 0) {
        return install_cached_obj(cached, obj_full);
      }
      unlink(tmp);
      return -1; /* out_err already set on compile failure */
    }
  }

  if (mkdir_for_file(obj_full) != 0) return -1;
  return run_clang_compile(b, src_full, obj_full, extra_define, wasm, out_err,
                           err_cap);
}

/* define buffer helper: "-DNAME=value" */
static void make_define(char *buf, size_t cap, const char *name, const char *value) {
  snprintf(buf, cap, "-D%s=%s", name, value);
}

/* ----------------------------------------------------------- compile jobs */

typedef struct {
  char src[1400];
  char obj_rel[600];
  char define[256];
  int wasm;
  int from_sdk;
} compile_job;

typedef struct {
  compile_job *jobs;
  size_t n, cap;
} compile_queue;

static void queue_push(compile_queue *q, const char *src, const char *obj_rel,
                       const char *define, int wasm, int from_sdk) {
  if (q->n == q->cap) {
    q->cap = q->cap ? q->cap * 2 : 64;
    q->jobs = (compile_job *)realloc(q->jobs, sizeof(compile_job) * q->cap);
    if (!q->jobs) die("out of memory");
  }
  compile_job *j = &q->jobs[q->n++];
  snprintf(j->src, sizeof(j->src), "%s", src);
  snprintf(j->obj_rel, sizeof(j->obj_rel), "%s", obj_rel);
  snprintf(j->define, sizeof(j->define), "%s", define ? define : "");
  j->wasm = wasm;
  j->from_sdk = from_sdk;
}

typedef struct {
  build_ctx *b;
  compile_queue *q;
  size_t next;
  pthread_mutex_t lock;
  char err[16384];
  size_t err_len;
  int failed;
} compile_pool;

static void *pool_worker(void *arg) {
  compile_pool *p = (compile_pool *)arg;
  for (;;) {
    pthread_mutex_lock(&p->lock);
    size_t i = p->next++;
    pthread_mutex_unlock(&p->lock);
    if (i >= p->q->n) break;
    compile_job *j = &p->q->jobs[i];
    char err[8192];
    if (compile_obj(p->b, j->src, j->obj_rel, j->define[0] ? j->define : NULL,
                    j->wasm, j->from_sdk, err, sizeof(err)) != 0) {
      pthread_mutex_lock(&p->lock);
      p->failed = 1;
      size_t room = sizeof(p->err) - p->err_len - 1;
      if (room > 0) {
        size_t len = strlen(err);
        if (len > room) len = room;
        memcpy(p->err + p->err_len, err, len);
        p->err_len += len;
        p->err[p->err_len++] = '\n';
        p->err[p->err_len] = 0;
      }
      pthread_mutex_unlock(&p->lock);
    }
  }
  return NULL;
}

/* run all queued compiles across ncpu workers; 0 on success */
static long cpu_count(void) {
#if defined(__APPLE__)
  int32_t n = 0;
  size_t len = sizeof(n);
  if (sysctlbyname("hw.ncpu", &n, &len, NULL, 0) == 0 && n > 0) return n;
  return 4;
#else
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  return n > 0 ? n : 4;
#endif
}

static int run_compile_pool(build_ctx *b, compile_queue *q) {
  if (q->n == 0) return 0;
  long ncpu = cpu_count();
  if (ncpu > 8) ncpu = 8;
  if (ncpu > (long)q->n) ncpu = (long)q->n;

  compile_pool p;
  memset(&p, 0, sizeof(p));
  p.b = b;
  p.q = q;
  pthread_mutex_init(&p.lock, NULL);

  if (ncpu == 1) {
    pool_worker(&p);
  } else {
    pthread_t tids[8];
    for (long i = 0; i < ncpu; i++) {
      if (pthread_create(&tids[i], NULL, pool_worker, &p) != 0) {
        ncpu = i; /* run the remainder on this thread */
        break;
      }
    }
    pool_worker(&p);
    for (long i = 0; i < ncpu; i++) pthread_join(tids[i], NULL);
  }
  pthread_mutex_destroy(&p.lock);
  if (p.failed) {
    fprintf(stderr, "%s\n", p.err);
    return -1;
  }
  return 0;
}

/* --------------------------------------------------------- source walking */

typedef struct {
  build_ctx *b;
  int wasm; /* current pass: 1 = wasm sources, 0 = server sources */
  char objprefix[32];
  compile_queue *q;
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

  queue_push(w->q, full, obj_rel, extra[0] ? extra : NULL, w->wasm, 0);
}

/* compile sdk runtime sources (native server side) */
static void compile_sdk_sources_cb(const char *rel, const char *full, int is_dir,
                                   void *user) {
  walk_ctx *w = (walk_ctx *)user;
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
  queue_push(w->q, full, obj_rel2, NULL, w->wasm, 1);
}

static void compile_wasm_runtime_cb(const char *rel, const char *full, int is_dir,
                                    void *user) {
  walk_ctx *w = (walk_ctx *)user;
  if (is_dir) return;
  if (strncmp(rel, "runtime/client/", 15) != 0) return;
  size_t rl = strlen(rel);
  if (rl < 2 || strcmp(rel + rl - 2, ".c") != 0) return;
  char mg[560];
  mangle(rel, mg, sizeof(mg));
  char obj_rel2[620];
  snprintf(obj_rel2, sizeof(obj_rel2), "wrt_%s.o", mg);
  queue_push(w->q, full, obj_rel2, NULL, 1, 1);
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
  char *buf = NULL;
  size_t blen = 0;
  FILE *f = open_memstream(&buf, &blen);
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
  int rc = write_if_changed(path, buf, blen) == 0 ? 0 : -1;
  free(buf);
  return rc;
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

/* Does this file have a real `#include ... cerco_client.h` directive?
 *
 * A plain substring search is not good enough: a route that documents the
 * client API, or renders a code sample, mentions the header inside a string
 * literal and is perfectly legal. Only a line whose first token is #include
 * counts, which is what the preprocessor would actually act on. */
static int includes_client_header(const char *data) {
  for (const char *line = data; line && *line;) {
    const char *end = strchr(line, '\n');
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (strncmp(p, "#include", 8) == 0) {
      const char *stop = end ? end : p + strlen(p);
      size_t rest = (size_t)(stop - (p + 8));
      const char *q = p + 8;
      for (size_t k = 0; k + 14 <= rest; k++) {
        if (strncmp(q + k, "cerco_client.h", 14) == 0) return 1;
      }
    }
    line = end ? end + 1 : NULL;
  }
  return 0;
}

/* fail the build if server-only code would end up in the wasm target */
static void audit_client_sources(build_ctx *b) {
  for (int i = 0; i < b->n_routes; i++) {
    char full[1400];
    snprintf(full, sizeof(full), "%s/%s", b->proj->root, b->routes[i].rel);
    size_t len = 0;
    char *data = read_file(full, &len);
    if (!data) continue;
    if (includes_client_header(data)) {
      free(data);
      /* say what went wrong: this used to exit silently */
      die("%s includes <cerco_client.h>, but route files are server-only "
          "and never enter the wasm binary.\n"
          "       Move the client code to src/components/ and mark its root "
          "with CERCO_COMPONENT().",
          b->routes[i].rel);
    }
    free(data);
  }
}

/* an output artifact is fresh when it is newer than every object and sdk
 * static lib that feeds it — lets no-op builds skip the linker entirely */
typedef struct {
  int64_t newest;
} freshness_ctx;

static void freshness_cb(const char *rel, const char *full, int is_dir,
                         void *user) {
  (void)rel;
  freshness_ctx *fc = (freshness_ctx *)user;
  if (is_dir) return;
  size_t l = strlen(full);
  if (l < 2 || strcmp(full + l - 2, ".o") != 0) return;
  int64_t mt;
  if (mtime_ms(full, &mt) == 0 && mt > fc->newest) fc->newest = mt;
}

static int outputs_up_to_date(const char *out, const char *objdir,
                              const char *sdk) {
  int64_t omt;
  if (mtime_ms(out, &omt) != 0) return 0;
  freshness_ctx fc = { 0 };
  walk_dir(objdir, "", freshness_cb, &fc);
  char lib[1400];
  static const char *libs[] = { "/lib/libuv.a", "/lib/libllhttp.a", NULL };
  for (int i = 0; libs[i]; i++) {
    snprintf(lib, sizeof(lib), "%s%s", sdk, libs[i]);
    int64_t mt;
    if (mtime_ms(lib, &mt) == 0 && mt > fc.newest) fc.newest = mt;
  }
  return omt > fc.newest;
}

static const char *wasm_out_path(build_ctx *b) {
  static char p[1400];
  snprintf(p, sizeof(p), "%s/assets/app.wasm", b->distdir);
  return p;
}

/* feed src/ contents into the tailwind cache key (classes can appear in
 * any scanned source file) */
static void css_hash_cb(const char *rel, const char *full, int is_dir,
                        void *user) {
  cerco_sha256_ctx *ctx = (cerco_sha256_ctx *)user;
  if (is_dir) return;
  if (strncmp(rel, "src/", 4) != 0) return;
  if (strcmp(rel, "src/styles.css") == 0) return; /* hashed separately */
  size_t len = 0;
  char *data = read_file(full, &len);
  if (!data) return;
  cerco_sha256_update(ctx, rel, strlen(rel));
  cerco_sha256_update(ctx, data, len);
  free(data);
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

  if (sdk_ensure(proj, b.sdk, sizeof(b.sdk), b.sdk_hash, sizeof(b.sdk_hash)) != 0)
    die("sdk extraction failed");
  find_clang(); /* warm the toolchain lookup before worker threads start */
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

  audit_client_sources(&b);

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
  compile_queue wq = {0};
  walk_ctx ww = { &b, 1, "wa", &wq };
  walk_dir(proj->root, "", compile_app_sources_cb, &ww);
  walk_dir(b.sdk, "", compile_wasm_runtime_cb, &ww);
  {
    char f[1400];
    char orl[128];
    snprintf(f, sizeof(f), "%s/sf_client.c", b.gendir);
    snprintf(orl, sizeof(orl), "wa_gen_sf_client.o");
    queue_push(&wq, f, orl, NULL, 1, 0);
    snprintf(f, sizeof(f), "%s/components.c", b.gendir);
    snprintf(orl, sizeof(orl), "wa_gen_components.o");
    queue_push(&wq, f, orl, NULL, 1, 0);
  }
  if (run_compile_pool(&b, &wq) != 0) return 1;
  free(wq.jobs);
  collect_ctx cw = { &b, &wasm_objs, "wa_" };
  walk_dir(b.objdir, "", collect_obj_cb, &cw);
  collect_ctx cw2 = { &b, &wasm_objs, "wrt_" };
  walk_dir(b.objdir, "", collect_obj_cb, &cw2);
  if (outputs_up_to_date(wasm_out_path(&b), b.objdir, b.sdk)) {
    if (g_verbose) printf("wasm up to date\n");
  } else if (link_wasm(&b, &wasm_objs, err, sizeof(err)) != 0) {
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
    /* output depends on the css entry + every src file's content (class
     * scanning); hash them and reuse the cached css when nothing changed */
    cerco_sha256_ctx hctx;
    cerco_sha256_init(&hctx);
    char css_in_full[1400];
    snprintf(css_in_full, sizeof(css_in_full), "%s/src/styles.css", proj->root);
    {
      size_t clen = 0;
      char *css = read_file(css_in_full, &clen);
      if (css) {
        cerco_sha256_update(&hctx, css, clen);
        free(css);
      }
    }
    walk_dir(proj->root, "", css_hash_cb, &hctx);
    uint8_t hd[32];
    cerco_sha256_final(&hctx, hd);
    char hexk[65];
    for (int i = 0; i < 32; i++) {
      static const char hx[] = "0123456789abcdef";
      hexk[i * 2] = hx[hd[i] >> 4];
      hexk[i * 2 + 1] = hx[hd[i] & 0xf];
    }
    hexk[64] = 0;
    const char *home = getenv("CERCO_CACHE_DIR");
    if (!home || !home[0]) home = getenv("HOME");
    int served = 0;
    if (home && home[0]) {
      char cc[1600];
      snprintf(cc, sizeof(cc), "%s/.cerco/cache/tw/%s.css", home, hexk);
      if (file_exists(cc)) {
        size_t clen = 0;
        char *css = read_file(cc, &clen);
        if (css) {
          write_file(css_out, css, clen);
          free(css);
          served = 1;
          if (g_verbose) printf("css cached (%s)\n", hexk);
        }
      }
      if (!served) {
        if (tailwind_run(proj, twbin, proj->root, "src/styles.css",
                         "dist/assets/styles.css", 0) != 0) {
          fprintf(stderr, "cerco: warning: tailwind build failed; continuing without css\n");
        } else {
          size_t clen = 0;
          char *css = read_file(css_out, &clen);
          if (css) {
            if (mkdir_for_file(cc) == 0) write_file(cc, css, clen);
            free(css);
          }
        }
        served = 1; /* attempted either way; don't fall through */
      }
    }
    if (!served && tailwind_run(proj, twbin, proj->root, "src/styles.css",
                                "dist/assets/styles.css", 0) != 0) {
      fprintf(stderr, "cerco: warning: tailwind build failed; continuing without css\n");
    }
  }

  /* ---- native server ---- */
  cerco_sb srv_objs;
  sb_init(&srv_objs);
  compile_queue sq = {0};
  walk_ctx sw = { &b, 0, "sa", &sq };
  walk_dir(proj->root, "", compile_app_sources_cb, &sw);
  walk_dir(b.sdk, "", compile_sdk_sources_cb, &sw);
  {
    char f[1400];
    char orl[128];
    snprintf(f, sizeof(f), "%s/routes.c", b.gendir);
    snprintf(orl, sizeof(orl), "sa_gen_routes.o");
    queue_push(&sq, f, orl, NULL, 0, 0);
    snprintf(f, sizeof(f), "%s/main.c", b.gendir);
    snprintf(orl, sizeof(orl), "sa_gen_main.o");
    queue_push(&sq, f, orl, NULL, 0, 0);
    snprintf(f, sizeof(f), "%s/sf_server.c", b.gendir);
    snprintf(orl, sizeof(orl), "sa_gen_sf_server.o");
    queue_push(&sq, f, orl, NULL, 0, 0);
  }
  if (gen_assets(&b) != 0) die("assets codegen failed");
  {
    char f[1400], orl[128];
    snprintf(f, sizeof(f), "%s/assets.c", b.gendir);
    snprintf(orl, sizeof(orl), "sa_gen_assets.o");
    queue_push(&sq, f, orl, NULL, 0, 0);
  }
  if (run_compile_pool(&b, &sq) != 0) return 1;
  free(sq.jobs);

  collect_ctx cs = { &b, &srv_objs, "sa_" };
  walk_dir(b.objdir, "", collect_obj_cb, &cs);
  collect_ctx cs2 = { &b, &srv_objs, "sdk_" };
  walk_dir(b.objdir, "", collect_obj_cb, &cs2);

  {
    char bin[1400];
    snprintf(bin, sizeof(bin), "%s/%s", b.distdir, proj->name);
    if (outputs_up_to_date(bin, b.objdir, b.sdk)) {
      if (g_verbose) printf("binary up to date\n");
    } else if (link_server(&b, &srv_objs, err, sizeof(err)) != 0) {
      fprintf(stderr, "server link failed:\n%s\n", err);
      return 1;
    }
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

