/* File-based router: generated static table, parsed once at startup.
 * Path format: "/users/:id", "/files/<rest>", "/". Matching is segment-based.
 */
#include "internal.h"
#include <string.h>
#include <stdlib.h>

#define CERCO_RT_MAX_SEGS 24

typedef struct cerco_rt_seg {
  const char *lit;    /* literal segment or NULL */
  const char *param;  /* ":name" param or NULL */
  int catchall;       /* trailing *rest */
} cerco_rt_seg;

typedef struct cerco_rt_route {
  const char *method;
  const char *orig_path;
  cerco_rt_seg segs[CERCO_RT_MAX_SEGS];
  int n_segs;
  cerco_route_fn fn;
} cerco_rt_route;

typedef struct cerco_rt_parsed {
  cerco_rt_route *routes;
  size_t n;
} cerco_rt_parsed;

static cerco_rt_parsed g_rt;

static int seg_parse(const char *p, cerco_rt_seg *out) {
  if (p[0] == ':') { out->param = p + 1; out->lit = NULL; return 1; }
  if (p[0] == '*' && p[1] != 0) { out->catchall = 1; out->param = p + 1; return 1; }
  out->lit = p;
  return 1;
}

int router_init(cerco_server *srv) {
  (void)srv;
  const cerco_app *app = srv->app;
  g_rt.n = app->n_routes;
  g_rt.routes = (cerco_rt_route *)calloc(g_rt.n ? g_rt.n : 1, sizeof(cerco_rt_route));
  if (!g_rt.routes) return -1;
  for (size_t i = 0; i < g_rt.n; i++) {
    const cerco_route_entry *e = &app->routes[i];
    cerco_rt_route *rr = &g_rt.routes[i];
    rr->method = e->method;
    rr->orig_path = e->path;
    rr->fn = e->fn;
    /* copy path segment strings (paths are static; we point into them) */
    const char *p = e->path;
    if (*p == '/') p++;
    while (*p && rr->n_segs < CERCO_RT_MAX_SEGS) {
      const char *end = strchr(p, '/');
      size_t len = end ? (size_t)(end - p) : strlen(p);
      char *seg = (char *)malloc(len + 1);
      if (!seg) return -1;
      memcpy(seg, p, len);
      seg[len] = 0;
      seg_parse(seg, &rr->segs[rr->n_segs]);
      rr->n_segs++;
      if (!end) break;
      p = end + 1;
    }
  }
  return 0;
}

static int seg_eq(const cerco_rt_seg *s, const char *req_seg) {
  if (s->catchall) return 1;
  if (s->param) return 1;
  return cerco_strcaseeq(s->lit, req_seg);
}

/* split request path into segments in place (modifies buf copy) */
static int split_path(char *path, char *segs[], int max) {
  int n = 0;
  char *p = path;
  if (*p == '/') p++;
  while (*p) {
    if (n >= max) return -1;
    segs[n++] = p;
    char *slash = strchr(p, '/');
    if (!slash) break;
    *slash = 0;
    p = slash + 1;
  }
  /* trailing slash: "/users/" -> segs = ["users"], fine */
  return n;
}

typedef enum { RT_MISS = 0, RT_MATCH, RT_METHOD_MISMATCH } rt_result;

/* tri-state route lookup; params captured into r on match */
static rt_result router_find(cerco_req *r, cerco_route_fn *out_fn, int *allowed_mask) {
  /* work on an arena copy: split_path writes NULs into separators */
  char *work = cerco_arena_strdup(r->arena, r->path);
  if (!work) return RT_MISS;
  char *segs[64];
  int n_segs = split_path(work, segs, 64);
  if (n_segs < 0) return RT_MISS;
  const char *method = cerco_method(r);
  int is_head = cerco_strcaseeq(method, "HEAD");
  *out_fn = NULL;

  rt_result result = RT_MISS;
  for (size_t i = 0; i < g_rt.n; i++) {
    cerco_rt_route *rr = &g_rt.routes[i];
    /* segment match */
    int matched = 1;
    int catchall_hit = 0;
    int seg_i = 0;
    for (int s = 0; s < rr->n_segs; s++) {
      if (rr->segs[s].catchall) { catchall_hit = 1; break; }
      if (seg_i >= n_segs) { matched = 0; break; }
      if (!seg_eq(&rr->segs[s], segs[seg_i])) { matched = 0; break; }
      seg_i++;
    }
    if (matched && !catchall_hit && seg_i != n_segs) matched = 0;
    if (!matched) continue;
    /* capture params (last match wins) */
    r->n_route_params = 0;
    seg_i = 0;
    for (int s = 0; s < rr->n_segs && seg_i < n_segs; s++) {
      if (rr->segs[s].catchall) {
        /* join remainder with '/' */
        size_t need = 1;
        for (int k = seg_i; k < n_segs; k++) need += strlen(segs[k]) + 1;
        char *joined = (char *)cerco_arena_alloc(r->arena, need);
        if (joined) {
          joined[0] = 0;
          size_t off = 0;
          for (int k = seg_i; k < n_segs; k++) {
            size_t l = strlen(segs[k]);
            if (k > seg_i) joined[off++] = '/';
            memcpy(joined + off, segs[k], l);
            off += l;
          }
          joined[off] = 0;
          if (r->n_route_params < 16) {
            r->route_params_name[r->n_route_params] = rr->segs[s].param;
            r->route_params_value[r->n_route_params] = joined;
            r->n_route_params++;
          }
        }
        break;
      }
      if (rr->segs[s].param) {
        if (r->n_route_params < 16) {
          r->route_params_name[r->n_route_params] = rr->segs[s].param;
          r->route_params_value[r->n_route_params] =
              cerco_arena_strdup(r->arena, segs[seg_i]);
          r->n_route_params++;
        }
      }
      seg_i++;
    }

    /* method match */
    const char *m = rr->method;
    if (cerco_strcaseeq(m, method) || (is_head && cerco_strcaseeq(m, "GET"))) {
      *out_fn = rr->fn;
      return RT_MATCH;
    }
    result = RT_METHOD_MISMATCH;
    if (allowed_mask) {
      if (cerco_strcaseeq(m, "GET")) *allowed_mask |= 0x1;
      else if (cerco_strcaseeq(m, "POST")) *allowed_mask |= 0x2;
      else if (cerco_strcaseeq(m, "PUT")) *allowed_mask |= 0x4;
      else if (cerco_strcaseeq(m, "PATCH")) *allowed_mask |= 0x8;
      else if (cerco_strcaseeq(m, "DELETE")) *allowed_mask |= 0x10;
      else if (cerco_strcaseeq(m, "HEAD")) *allowed_mask |= 0x20;
      else if (cerco_strcaseeq(m, "OPTIONS")) *allowed_mask |= 0x40;
    }
  }
  return result;
}

void router_send_404(cerco_req *r) {
  /* custom 404 route? (generated path /__cerco/404) */
  for (size_t i = 0; i < g_rt.n; i++) {
    if (cerco_strcaseeq(g_rt.routes[i].method, "GET") &&
        cerco_strcaseeq(g_rt.routes[i].orig_path, "/__cerco/404")) {
      r->status = 404;
      r->handler = g_rt.routes[i].fn;
      if (wpool_try_submit(r->conn->srv, r) == 0) return;
      break;
    }
  }
  r->status = 404;
  wbuf_reset(&r->resp);
  wbuf_puts(&r->resp, "<!doctype html><title>404</title><h1>404 Not Found</h1>");
  conn_start_response(r);
}

void router_send_405(cerco_req *r, int allowed_mask) {
  char allow[128];
  int off = 0;
  if (allowed_mask & 0x1) off += snprintf(allow + off, sizeof(allow) - off, "%sGET", off ? ", " : "");
  if (allowed_mask & 0x2) off += snprintf(allow + off, sizeof(allow) - off, "%sPOST", off ? ", " : "");
  if (allowed_mask & 0x4) off += snprintf(allow + off, sizeof(allow) - off, "%sPUT", off ? ", " : "");
  if (allowed_mask & 0x8) off += snprintf(allow + off, sizeof(allow) - off, "%sPATCH", off ? ", " : "");
  if (allowed_mask & 0x10) off += snprintf(allow + off, sizeof(allow) - off, "%sDELETE", off ? ", " : "");
  if (allowed_mask & 0x40) off += snprintf(allow + off, sizeof(allow) - off, "%sOPTIONS", off ? ", " : "");
  if (!off) snprintf(allow, sizeof(allow), "GET, HEAD, OPTIONS");
  char extra[160];
  snprintf(extra, sizeof(extra), "Allow: %s", allow);
  r->status = 405;
  conn_send_simple(r->conn, 405, extra, "method not allowed\n", 19, 0);
}

int router_probe(cerco_req *r, cerco_route_fn *fn, int *allowed_mask) {
  rt_result res = router_find(r, fn, allowed_mask);
  return res == RT_MATCH ? 1 : res == RT_METHOD_MISMATCH ? 2 : 0;
}

void router_dispatch(cerco_req *r) {
  cerco_route_fn fn = NULL;
  int allowed = 0;
  rt_result res = router_find(r, &fn, &allowed);
  if (res == RT_MATCH) {
    r->handler = fn;
    if (wpool_try_submit(r->conn->srv, r) != 0) {
      atomic_fetch_add(&r->conn->srv->stats.rejected_jobs, 1);
      conn_send_simple(r->conn, 503, "Retry-After: 1", "server busy\n", 12, 0);
    }
    return;
  }
  if (res == RT_METHOD_MISMATCH) {
    if (cerco_strcaseeq(cerco_method(r), "OPTIONS")) router_send_405(r, allowed | 0x40);
    else router_send_405(r, allowed);
    return;
  }
  router_send_404(r);
}
