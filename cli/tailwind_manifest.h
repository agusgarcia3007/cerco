/* pinned Tailwind standalone manifest — generated from official releases.
 * The CLI refuses to execute a binary whose SHA-256 does not match. */
#ifndef CERCO_TAILWIND_MANIFEST_H
#define CERCO_TAILWIND_MANIFEST_H

#define TAILWIND_PINNED_VERSION "4.3.3"

typedef struct {
  const char *version;
  const char *platform; /* darwin | linux */
  const char *arch;     /* arm64 | x64 */
  const char *url;
  const char *sha256;
} tailwind_entry;

static const tailwind_entry tailwind_manifest[] = {
  { "4.3.3", "darwin", "arm64",
    "https://github.com/tailwindlabs/tailwindcss/releases/download/v4.3.3/tailwindcss-macos-arm64",
    "cdf646702987a743464dff4d9c60fd4480d1c1e73dd819a9a67f1078815dce9d" },
  { "4.3.3", "darwin", "x64",
    "https://github.com/tailwindlabs/tailwindcss/releases/download/v4.3.3/tailwindcss-macos-x64",
    "7922e0953f2110c05976e3bf58f14e643d90427575e766b7d433f5f80cbee7e1" },
  { "4.3.3", "linux", "arm64",
    "https://github.com/tailwindlabs/tailwindcss/releases/download/v4.3.3/tailwindcss-linux-arm64",
    "55fd0b241214eff3de1e8ee4f22796662f2d2e7a49bcfca7477cfd0bac398195" },
  { "4.3.3", "linux", "x64",
    "https://github.com/tailwindlabs/tailwindcss/releases/download/v4.3.3/tailwindcss-linux-x64",
    "dc61b3ac6b8c9ca874c0cc4c57b2409791a64c5540404ca5f5367360babc313a" },
};

#define TAILWIND_MANIFEST_COUNT 4

#endif
