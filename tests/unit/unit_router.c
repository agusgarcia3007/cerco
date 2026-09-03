/* router tests: exercise router_init + router_probe through a shim server */
#include "test.h"
#include "arena.h"
#include "str.h"
#include "internal.h"

static void fake_req(cerco_req *r, cerco_arena *a, const char *method, const char *path) {
  memset(r, 0, sizeof(*r));
  r->arena = a;
  r->method = cerco_arena_strdup(a, method);
  r->path = cerco_arena_strdup(a, path);
  r->query = (char *)"";
  r->status = 200;
}

static void rt_page(cerco_req *r) { (void)r; }
static void rt_page_post(cerco_req *r) { (void)r; }
static void rt_users(cerco_req *r) { (void)r; }
static void rt_user_id(cerco_req *r) { (void)r; }
static void rt_files(cerco_req *r) { (void)r; }

TEST(router_matching) {
  static cerco_route_entry routes[] = {
    { "GET", "/", rt_page },
    { "POST", "/submit", rt_page_post },
    { "GET", "/users", rt_users },
    { "GET", "/users/:id", rt_user_id },
    { "GET", "/files/*rest", rt_files },
  };
  cerco_app app = { .name = "t", .routes = routes, .n_routes = 5 };
  cerco_server srv;
  memset(&srv, 0, sizeof(srv));
  srv.app = &app;
  CHECK(router_init(&srv) == 0);

  cerco_arena a;
  cerco_arena_init(&a, 4096, 0);
  cerco_req r;

  /* exact match */
  fake_req(&r, &a, "GET", "/");
  cerco_route_fn fn = NULL;
  int allowed = 0;
  CHECK_INT_EQ(router_probe(&r, &fn, &allowed), 1);
  CHECK(fn == rt_page);

  /* method mismatch */
  fake_req(&r, &a, "DELETE", "/submit");
  CHECK_INT_EQ(router_probe(&r, &fn, &allowed), 2);
  CHECK_INT_EQ(allowed & 0x2, 0x2); /* POST allowed */

  /* dynamic param */
  fake_req(&r, &a, "GET", "/users/42");
  CHECK_INT_EQ(router_probe(&r, &fn, &allowed), 1);
  CHECK(fn == rt_user_id);
  CHECK_INT_EQ(r.n_route_params, 1);
  CHECK_STR_EQ(r.route_params_name[0], "id");
  CHECK_STR_EQ(r.route_params_value[0], "42");

  /* param must not match across slashes */
  fake_req(&r, &a, "GET", "/users/42/posts");
  CHECK_INT_EQ(router_probe(&r, &fn, &allowed), 0);

  /* catch-all */
  fake_req(&r, &a, "GET", "/files/a/b/c.txt");
  CHECK_INT_EQ(router_probe(&r, &fn, &allowed), 1);
  CHECK(fn == rt_files);
  CHECK_INT_EQ(r.n_route_params, 1);
  CHECK_STR_EQ(r.route_params_value[0], "a/b/c.txt");

  /* 404 */
  fake_req(&r, &a, "GET", "/nope");
  CHECK_INT_EQ(router_probe(&r, &fn, &allowed), 0);

  /* trailing slash tolerated */
  fake_req(&r, &a, "GET", "/users/");
  CHECK_INT_EQ(router_probe(&r, &fn, &allowed), 1);

  /* path is not modified by matching (dispatch relies on it) */
  fake_req(&r, &a, "GET", "/users/7");
  router_probe(&r, &fn, &allowed);
  CHECK_STR_EQ(r.path, "/users/7");

  cerco_arena_destroy(&a);
}

void main_router(void) { test_router_matching(); }
