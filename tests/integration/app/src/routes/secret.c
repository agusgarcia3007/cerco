#include <cerco.h>

/* server-only secret that must never appear in the wasm client binary */
static const char *SECRET_TOKEN = "S3cretServerOnlyToken99";

CERCO_ROUTE {
  cerco_raw(r, "secret-len:");
  char buf[8];
  int n = 0;
  int v = (int)__builtin_strlen(SECRET_TOKEN);
  while (v) { buf[n++] = (char)('0' + v % 10); v /= 10; }
  while (n) cerco_write_bytes(r, &buf[--n], 1);
}
