#define _POSIX_C_SOURCE 200809L
#include "main.h"
#include "util.h"
#include "bundle.h"
#include "sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* a bundle path feeds the compilers only when it matches one of these */
static int is_compile_input(const char *path) {
  if (strncmp(path, "include/", 8) == 0) return 1;
  if (strncmp(path, "vendor/", 7) == 0) return 1;
  if (strncmp(path, "lib/", 4) == 0) return 1;
  if (strncmp(path, "runtime/client/", 15) == 0) return 1;
  if (strncmp(path, "runtime/server/", 15) == 0) return 1;
  if (strncmp(path, "runtime/shared/", 15) == 0) return 1;
  return 0; /* templates/, runtime/browser/host.js: never compiled */
}

/* Extract the embedded SDK into <root>/.cerco/sdk if needed.
 * Marker file name = hash of bundle contents, so CLI/header updates
 * invalidate the cache automatically.
 * out_hash receives a hash of only the compile-relevant inputs (plus the
 * CLI version): templates/host.js edits must NOT invalidate the shared
 * object cache in ~/.cerco/cache, real runtime/header changes must.
 * Returns 0 on success. out_dir receives the sdk path. */
int sdk_ensure(cerco_project *proj, char *out_dir, size_t out_cap,
               char *out_hash, size_t hash_cap) {
  char sdk[1200], marker[1400];
  snprintf(sdk, sizeof(sdk), "%s/.cerco/sdk", proj->root);

  /* two hashes over the bundle: everything (extraction marker) and only
   * what reaches the compilers (object cache key) */
  cerco_sha256_ctx ctx, cctx;
  cerco_sha256_init(&ctx);
  cerco_sha256_init(&cctx);
  cerco_sha256_update(&cctx, CERCO_VERSION, strlen(CERCO_VERSION));
  for (unsigned i = 0; i < CERCO_BUNDLE_COUNT; i++) {
    const bundle_file *f = &cerco_bundle[i];
    uint8_t lenb[4] = { (uint8_t)f->len, (uint8_t)(f->len >> 8),
                        (uint8_t)(f->len >> 16), (uint8_t)(f->len >> 24) };
    cerco_sha256_update(&ctx, f->path, strlen(f->path));
    cerco_sha256_update(&ctx, lenb, 4);
    cerco_sha256_update(&ctx, f->data, f->len);
    if (is_compile_input(f->path)) {
      cerco_sha256_update(&cctx, f->path, strlen(f->path));
      cerco_sha256_update(&cctx, lenb, 4);
      cerco_sha256_update(&cctx, f->data, f->len);
    }
  }
  uint8_t digest[32];
  cerco_sha256_final(&ctx, digest);
  char hex[65];
  for (int i = 0; i < 16; i++) {
    static const char hd[] = "0123456789abcdef";
    hex[i * 2] = hd[digest[i] >> 4];
    hex[i * 2 + 1] = hd[digest[i] & 0xf];
  }
  hex[32] = 0;
  if (out_hash && hash_cap > 0) {
    uint8_t cdigest[32];
    cerco_sha256_final(&cctx, cdigest);
    char chex[65];
    for (int i = 0; i < 16; i++) {
      static const char hd[] = "0123456789abcdef";
      chex[i * 2] = hd[cdigest[i] >> 4];
      chex[i * 2 + 1] = hd[cdigest[i] & 0xf];
    }
    chex[32] = 0;
    snprintf(out_hash, hash_cap, "%s", chex);
  }

  char oldmarker[1400];
  snprintf(oldmarker, sizeof(oldmarker), "%s/.cerco/sdk_marker", proj->root);
  snprintf(marker, sizeof(marker), "%s/%s", sdk, hex);

  if (file_exists(marker) && file_exists(oldmarker)) {
    /* verify the marker matches (cheap) */
    size_t ml = 0;
    char *m = read_file(oldmarker, &ml);
    if (m && strcmp(m, marker) == 0) {
      free(m);
      snprintf(out_dir, out_cap, "%s", sdk);
      return 0;
    }
    free(m);
  }
  rm_recursive(sdk);
  if (mkdirs(sdk) != 0) die("cannot create %s", sdk);
  for (unsigned i = 0; i < CERCO_BUNDLE_COUNT; i++) {
    const bundle_file *f = &cerco_bundle[i];
    char dest[1400];
    snprintf(dest, sizeof(dest), "%s/%s", sdk, f->path);
    if (mkdir_for_file(dest) != 0) die("mkdir failed for %s", dest);
    if (write_file(dest, f->data, f->len) != 0) die("write failed for %s", dest);
  }
  if (write_file(marker, "", 0) != 0) die("cannot write marker");
  write_file(oldmarker, marker, strlen(marker));
  if (g_verbose) printf("sdk extracted to %s\n", sdk);
  snprintf(out_dir, out_cap, "%s", sdk);
  return 0;
}
