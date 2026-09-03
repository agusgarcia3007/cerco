/* Request dispatch: reserved endpoints -> routes -> static assets -> 404.
 * Runs on the event loop; dynamic work is submitted to the worker pool. */
#include "internal.h"
#include <string.h>
#include <stdio.h>

int router_probe(cerco_req *r, cerco_route_fn *fn, int *allowed_mask);
void router_send_405(cerco_req *r, int allowed_mask);

void conn_request_dispatch(cerco_conn *c) {
  cerco_server *srv = c->srv;
  cerco_req *r = c->req;
  if (!r || c->bad_request) return;
  const char *path = r->path;
  const char *method = cerco_method(r);
  int get_like = cerco_strcaseeq(method, "GET") || cerco_strcaseeq(method, "HEAD");

  /* server functions */
  if (sf_is_sf_path(path)) {
    sf_dispatch(r, path + 12);
    return;
  }

  /* reserved endpoints */
  if (cerco_strncaseeq(path, "/__cerco/", 9)) {
    sf_handle_reserved(r);
    return;
  }

  /* routes */
  cerco_route_fn fn = NULL;
  int allowed = 0;
  int res = router_probe(r, &fn, &allowed);
  if (res == 1) {
    r->handler = fn;
    if (wpool_try_submit(srv, r) != 0) {
      atomic_fetch_add(&srv->stats.rejected_jobs, 1);
      conn_send_simple(c, 503, "Retry-After: 1", "server busy\n", 12, 0);
    }
    return;
  }
  if (res == 2) {
    if (cerco_strcaseeq(method, "OPTIONS")) {
      r->status = 204;
      char allow[128];
      int off = 0;
      if (allowed & 0x1) off += snprintf(allow + off, sizeof(allow) - off, "%sGET", off ? ", " : "");
      if (allowed & 0x2) off += snprintf(allow + off, sizeof(allow) - off, "%sPOST", off ? ", " : "");
      if (allowed & 0x4) off += snprintf(allow + off, sizeof(allow) - off, "%sPUT", off ? ", " : "");
      if (allowed & 0x8) off += snprintf(allow + off, sizeof(allow) - off, "%sPATCH", off ? ", " : "");
      if (allowed & 0x10) off += snprintf(allow + off, sizeof(allow) - off, "%sDELETE", off ? ", " : "");
      if (!off) snprintf(allow, sizeof(allow), "GET, HEAD, OPTIONS");
      char extra[160];
      snprintf(extra, sizeof(extra), "Allow: %s", allow);
      conn_send_simple(c, 204, extra, NULL, 0, 0);
      return;
    }
    router_send_405(r, allowed);
    return;
  }

  /* static assets (event loop, no worker) */
  if (get_like && static_is_asset_path(path)) {
    static_try_serve(r);
    return;
  }

  router_send_404(r);
}
