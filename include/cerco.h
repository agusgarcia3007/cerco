/* cerco — server-side public API.
 *
 * Application code (routes, layouts, components) includes this header.
 * Handlers write HTML incrementally into the request writer; the framework
 * stages the response in the request arena and streams it to the socket from
 * the event loop after the worker completes.
 */
#ifndef CERCO_H
#define CERCO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cerco_req cerco_req;

/* ------------------------------------------------------------------ request */

const char *cerco_method(cerco_req *r);            /* "GET", "POST", ... */
const char *cerco_path(cerco_req *r);              /* decoded, e.g. "/users/42" */
const char *cerco_query(cerco_req *r);             /* raw query string or "" */
const char *cerco_param(cerco_req *r, const char *name);       /* route param */
const char *cerco_query_get(cerco_req *r, const char *name);   /* query param */
const char *cerco_header(cerco_req *r, const char *name);      /* case-insensitive */
const char *cerco_cookie(cerco_req *r, const char *name);
const char *cerco_body(cerco_req *r, size_t *len); /* NULL if absent */
const char *cerco_form(cerco_req *r, const char *name);        /* urlencoded body */
const char *cerco_json_string(cerco_req *r, const char *field);/* body as json */
const char *cerco_remote_addr(cerco_req *r);       /* "ip:port" */

/* 1 when the client disconnected or the request timed out; workers should
 * check this between chunks of long work and bail out early */
int cerco_cancelled(cerco_req *r);

/* --------------------------------------------------------------- response */

void cerco_status(cerco_req *r, int code);
void cerco_set_header(cerco_req *r, const char *name, const char *value);
void cerco_add_header(cerco_req *r, const char *name, const char *value);

typedef enum {
  CERCO_COOKIE_HTTPONLY = 1 << 0,
  CERCO_COOKIE_SECURE = 1 << 1,
  CERCO_COOKIE_SAMESITE_LAX = 1 << 2,
  CERCO_COOKIE_SAMESITE_STRICT = 1 << 3,
  CERCO_COOKIE_SAMESITE_NONE = 1 << 4,
} cerco_cookie_flags;

void cerco_set_cookie(cerco_req *r, const char *name, const char *value,
                      int max_age, const char *path, unsigned flags);
void cerco_redirect(cerco_req *r, const char *location); /* 302 (303 after POST) */

/* Set this page's <title>, overriding the one the layout rendered. Call it
 * anywhere in a route handler: the document is rewritten after the handler
 * returns, so ordering does not matter. The text is escaped. */
void cerco_title(cerco_req *r, const char *title);

/* ------------------------------------------------------------ HTML writing */

/* All text/attribute writes are HTML-escaped by default. */
void cerco_text(cerco_req *r, const char *s);
/* printf-style; every string argument is escaped, literals are not */
void cerco_textf(cerco_req *r, const char *fmt, ...);
/* DANGER: writes bytes verbatim, no escaping */
void cerco_raw(cerco_req *r, const char *s);
void cerco_write_bytes(cerco_req *r, const void *data, size_t len);

typedef struct { const char *k; const char *v; } cerco_attr;

/* attribute values are escaped; keys must be safe literals.
 * Use CERCO_ATTRS(...) for several pairs, CERCO_CLASS("x") for one class. */
#define CERCO_ATTRS(...) (const cerco_attr[]){ __VA_ARGS__, {0, 0} }
#define CERCO_CLASS(v) (const cerco_attr[]){ {"class", v}, {0, 0} }

void cerco_tag_open(cerco_req *r, const char *tag, const cerco_attr *attrs);
void cerco_tag_close(cerco_req *r, const char *tag);

/* natural nesting without function pointers:
 *   cerco_tag(r, "div", CERCO_ATTRS(CERCO_CLASS("card"))) {
 *     cerco_tag(r, "p", NULL) { cerco_text(r, "hi"); }
 *   }
 */
#define cerco_tag(r, tag, attrs)                                              \
  for (int _cerco_once_ = (cerco_tag_open(r, tag, attrs), 1); _cerco_once_;    \
       _cerco_once_ = 0, cerco_tag_close(r, tag))

/* <input/>, <br/>, <img .../> ... */
void cerco_void_tag(cerco_req *r, const char *tag, const cerco_attr *attrs);

/* ------------------------------------------------- interactive components */

/* escape for embedding inside data-cerco-props (only needed when building
 * attrs manually; CERCO_COMPONENT values are escaped once by cerco_tag_open) */
const char *cerco_prop_escape(cerco_req *r, const char *json);

/* Marks the current open tag as an interactive component root:
 *   cerco_tag(r, "div", CERCO_COMPONENT(r, "counter", "{\"start\":0}")) { ... }
 * The client runtime hydrates it by calling the component's mount function.
 */
#define CERCO_COMPONENT(r, name, props_json)                                  \
  (const cerco_attr[]){ {"data-cerco", name},                                  \
                        {"data-cerco-props", props_json},                      \
                        {0, 0} }

/* -------------------------------------------------------- routing / layouts */

/* route handler symbol: the build passes -DCERCO_ROUTE_SYMBOL=<mangled> */
#ifndef CERCO_ROUTE_SYMBOL
#define CERCO_ROUTE_SYMBOL cerco_route_unnamed
#endif
/* method variants; build also passes -DCERCO_ROUTE_SYMBOL per file */
#define CERCO_ROUTE        void CERCO_ROUTE_SYMBOL(cerco_req *r)
#define CERCO_ROUTE_POST   CERCO_ROUTE
#define CERCO_ROUTE_PUT    CERCO_ROUTE
#define CERCO_ROUTE_PATCH  CERCO_ROUTE
#define CERCO_ROUTE_DELETE CERCO_ROUTE
#define CERCO_ROUTE_HEAD   CERCO_ROUTE
#define CERCO_ROUTE_OPTIONS CERCO_ROUTE

/* layout file: wraps children; call cerco_layout_children(r) to render next */
#ifndef CERCO_LAYOUT_SYMBOL
#define CERCO_LAYOUT_SYMBOL cerco_layout_unnamed
#endif
#define CERCO_LAYOUT                                                          \
  void CERCO_LAYOUT_SYMBOL(cerco_req *r, void (*children)(cerco_req *))
#define cerco_layout_children(req) children(req)

/* ------------------------------------------------------------ server funcs */

typedef struct cerco_sf_ctx cerco_sf_ctx;

typedef struct {
  const uint8_t *data;
  size_t len;
} cerco_bytes;

void cerco_sf_fail(cerco_sf_ctx *ctx, const char *message);
/* arena the handler may use for returned strings/buffers (request lifetime) */
void *cerco_sf_arena(cerco_sf_ctx *ctx);

/* -------------------------------------------------------------- app object */

typedef void (*cerco_route_fn)(cerco_req *r);

typedef struct {
  const char *method; /* "GET" | "POST" | ... ; "HEAD" auto-added for GET */
  const char *path;   /* "/users/:id", catch-all "/files/<rest>" */
  cerco_route_fn fn;
} cerco_route_entry;

typedef void (*cerco_layout_fn)(cerco_req *r, void (*children)(cerco_req *));

/* server-function entry; generated from the app's X-macro declarations */
typedef struct {
  uint32_t id;
  const char *method;  /* HTTP method gate */
  const char *name;    /* function name (debug) */
  void (*call)(cerco_req *req, cerco_sf_ctx *ctx); /* generated adapter */
} cerco_sf_entry;

typedef struct {
  const char *path;      /* url path, e.g. "/assets/app.wasm" */
  const char *mime;
  const char *etag;      /* strong etag (hex, no quotes) */
  const uint8_t *bytes;
  size_t len;
} cerco_asset_entry;

/* ------------------------------------------------------------- entry point */

typedef struct {
  const char *name;
  const cerco_route_entry *routes;
  size_t n_routes;
  cerco_layout_fn root_layout; /* may be NULL */
  const cerco_sf_entry *sfs;
  size_t n_sfs;
  const cerco_asset_entry *assets;
  size_t n_assets;
} cerco_app;

int cerco_serve(const cerco_app *app, int argc, char **argv);

/* internal: called by generated main; handlers should not use */
void *cerco__req_arena(cerco_req *r);

#ifdef __cplusplus
}
#endif

#endif
