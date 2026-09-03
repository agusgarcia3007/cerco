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

/* Extract the embedded SDK into <root>/.cerco/sdk if needed.
 * Marker file name = hash of bundle contents, so CLI/header updates
 * invalidate the cache automatically.
 * Returns 0 on success. out_dir receives the sdk path. */
int sdk_ensure(cerco_project *proj, char *out_dir, size_t out_cap) {
  char sdk[1200], marker[1400];
  snprintf(sdk, sizeof(sdk), "%s/.cerco/sdk", proj->root);

  /* content hash of the bundle */
  cerco_sha256_ctx ctx;
  cerco_sha256_init(&ctx);
  for (unsigned i = 0; i < CERCO_BUNDLE_COUNT; i++) {
    const bundle_file *f = &cerco_bundle[i];
    cerco_sha256_update(&ctx, f->path, strlen(f->path));
    uint8_t lenb[4] = { (uint8_t)f->len, (uint8_t)(f->len >> 8),
                        (uint8_t)(f->len >> 16), (uint8_t)(f->len >> 24) };
    cerco_sha256_update(&ctx, lenb, 4);
    cerco_sha256_update(&ctx, f->data, f->len);
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
