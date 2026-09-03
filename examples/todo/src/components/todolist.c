#include <cerco_client.h>
#include <stdint.h>

static void added(int32_t result, const char *err, void *user) {
  (void)user;
  if (err) {
    cerco_debug_log(err);
    return;
  }
  /* state changed on the server: reload list region */
  cerco_navigate(cerco_current_path());
}

static void add_todo(const char *text) {
  cerco_sf_todo_add(text, added, 0);
}

/* exposed for the demo via a click handler on the hint text */
static void on_hint_click(int32_t node, void *user) {
  (void)node; (void)user;
  add_todo("buy milk");
}

CERCO_CLIENT_COMPONENT(todolist) {
  int32_t root = cerco_root_node(cerco_root);
  int32_t list = cerco_query(root, "[data-cerco-b=list]");
  cerco_on(list, "click", on_hint_click, 0);
}
