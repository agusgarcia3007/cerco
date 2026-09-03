#include <cerco.h>

int32_t sf_itest_add(cerco_sf_ctx *ctx, int32_t a, int32_t b) { return a + b; }
int32_t sf_itest_fail(cerco_sf_ctx *ctx) {
  cerco_sf_fail(ctx, "intentional");
  return 0;
}
