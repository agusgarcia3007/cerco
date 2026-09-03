#include <cerco.h>

int32_t sf_add(cerco_sf_ctx *ctx, int32_t a, int32_t b) {
  if (a < 0 || b < 0) {
    cerco_sf_fail(ctx, "negative numbers not allowed");
    return 0;
  }
  return a + b;
}
