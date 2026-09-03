/* Static assets.
 *  - release: exact-match lookup in the embedded asset table (no fs access).
 *  - dev: serve from CERCO_DIST_DIR on disk with traversal protection.
 */
#include "internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <stdio.h>

static int if_none_match_hits(cerco_req *r, const char *etag) {
  const char *inm = cerco_header(r, "if-none-match");
  if (!inm) return 0;
  /* weak compare: find etag inside the list */
  return strstr(inm, etag) != NULL;
}

static void serve_asset_bytes(cerco_req *r, const char *mime, const char *etag,
                              const uint8_t *bytes, size_t len, int cacheable) {
  if (if_none_match_hits(r, etag)) {
    r->status = 304;
    char v[128];
    snprintf(v, sizeof(v), "\"%s\"", etag);
    cerco_set_header(r, "ETag", v);
    cerco_set_header(r, "Cache-Control", "public, max-age=300");
    conn_start_response(r);
    return;
  }
  r->status = 200;
  cerco_set_header(r, "Content-Type", mime);
  char v[128];
  snprintf(v, sizeof(v), "\"%s\"", etag);
  cerco_set_header(r, "ETag", v);
  if (cacheable) cerco_set_header(r, "Cache-Control", "public, max-age=300");
  else cerco_set_header(r, "Cache-Control", "no-store");
  cerco_write_bytes(r, bytes, len);
  conn_start_response(r);
}

static int dev_serve_from_disk(cerco_req *r) {
  cerco_server *srv = r->conn->srv;
  const char *base = srv->cfg.dist_dir;
  if (!base || !base[0]) return 0;
  const char *path = r->path;
  if (strstr(path, "..")) return 0; /* traversal attempt */
  char full[PATH_MAX];
  int n = snprintf(full, sizeof(full), "%s%s", base, path);
  if (n <= 0 || (size_t)n >= sizeof(full)) return 0;
  /* resolve and enforce prefix */
  char resolved[PATH_MAX];
  if (!realpath(full, resolved)) return 0;
  char base_real[PATH_MAX];
  if (!realpath(base, base_real)) return 0;
  size_t bl = strlen(base_real);
  if (strncmp(resolved, base_real, bl) != 0 ||
      (resolved[bl] != '/' && resolved[bl] != 0))
    return 0;

  FILE *f = fopen(resolved, "rb");
  if (!f) return 0;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz < 0 || (unsigned long)sz > srv->cfg.max_body * 4) { fclose(f); return 0; }
  uint8_t *buf = (uint8_t *)malloc(sz > 0 ? (size_t)sz : 1);
  if (!buf) { fclose(f); return 0; }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return 0; }
  fclose(f);

  char etag[65];
  cerco_sha256_hex(buf, (size_t)sz, etag);
  /* truncate etag to 16 hex chars for smaller headers */
  etag[16] = 0;
  serve_asset_bytes(r, cerco_mime_from_ext(resolved), etag, buf, (size_t)sz, 0);
  free(buf);
  return 1;
}

int static_is_asset_path(const char *path) {
  /* assets always carry a file extension and are not root */
  const char *dot = strrchr(path, '.');
  if (!dot || dot == path) return 0;
  const char *slash = strrchr(path, '/');
  if (slash && slash > dot) return 0;
  return dot[1] != 0;
}

void static_try_serve(cerco_req *r) {
  cerco_server *srv = r->conn->srv;
  const cerco_app *app = srv->app;
  /* embedded first (release) */
  if (app->assets) {
    for (size_t i = 0; i < app->n_assets; i++) {
      if (cerco_strcaseeq(app->assets[i].path, r->path)) {
        serve_asset_bytes(r, app->assets[i].mime, app->assets[i].etag,
                          app->assets[i].bytes, app->assets[i].len, 1);
        return;
      }
    }
  }
  if (srv->cfg.dev_mode) {
    if (dev_serve_from_disk(r)) return;
  }
  router_send_404(r);
}
