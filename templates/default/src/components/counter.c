#include <cerco_client.h>

typedef struct {
  cerco_sig *count;
  int32_t msg;
} counter_state;

static void on_inc(int32_t node, void *user) {
  (void)node;
  counter_state *st = (counter_state *)user;
  cerco_signal_set(st->count, cerco_signal_get(st->count) + 1);
  cerco_set_text(st->msg, "signals are live (wasm resumed)");
}

static void on_dec(int32_t node, void *user) {
  (void)node;
  counter_state *st = (counter_state *)user;
  cerco_signal_set(st->count, cerco_signal_get(st->count) - 1);
}

CERCO_CLIENT_COMPONENT(counter) {
  int32_t root = cerco_root_node(cerco_root);
  int32_t inc = cerco_query(root, "[data-cerco-b=inc]");
  int32_t dec = cerco_query(root, "[data-cerco-b=dec]");
  int32_t val = cerco_query(root, "[data-cerco-b=value]");
  int32_t msg = cerco_query(root, "[data-cerco-b=msg]");

  counter_state *st = (counter_state *)cerco_alloc(sizeof(counter_state));
  st->count = cerco_signal_new(cerco_json_int(cerco_props, "start", 0));
  st->msg = msg;
  cerco_bind_text(val, st->count);
  cerco_on(inc, "click", on_inc, st);
  cerco_on(dec, "click", on_dec, st);
}
