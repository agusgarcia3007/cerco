#include <cerco_client.h>
#include <stdint.h>

static void on_sum_result(int32_t result, const char *err, void *user);

static void on_sum(int32_t node, void *user) {
  (void)node;
  cerco_sf_add(2, 3, on_sum_result, user);
}

static void on_sum_result(int32_t result, const char *err, void *user) {
  int32_t out = (int32_t)(intptr_t)user;
  if (err) {
    cerco_set_text(out, err);
    return;
  }
  char buf[16];
  int i = 0;
  int32_t v = result;
  char tmp[12];
  if (v == 0) { tmp[i++] = '0'; }
  while (v) { tmp[i++] = (char)('0' + v % 10); v /= 10; }
  int o = 0;
  while (i) buf[o++] = tmp[--i];
  buf[o] = 0;
  cerco_set_text(out, buf);
}

CERCO_CLIENT_COMPONENT(sumdemo) {
  int32_t root = cerco_root_node(cerco_root);
  int32_t go = cerco_query(root, "[data-cerco-b=go]");
  int32_t out = cerco_query(root, "[data-cerco-b=result]");
  cerco_on(go, "click", on_sum, (void *)(intptr_t)out);
}
