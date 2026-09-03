#define _POSIX_C_SOURCE 200809L
#include "main.h"
#include "util.h"
#include "bundle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void substitute(const char *data, size_t len, const char *name, cerco_sb *out) {
  /* replace "my-app" placeholders in templates */
  size_t nlen = strlen(name);
  size_t i = 0;
  while (i < len) {
    if (i + 6 <= len && memcmp(data + i, "my-app", 6) == 0) {
      sb_putn(out, name, nlen);
      i += 6;
    } else {
      sb_putc(out, data[i]);
      i++;
    }
  }
}

static void copy_template_dir(const char *src_prefix, const char *dest_root,
                              const char *name) {
  for (unsigned i = 0; i < CERCO_BUNDLE_COUNT; i++) {
    const bundle_file *f = &cerco_bundle[i];
    if (strncmp(f->path, src_prefix, strlen(src_prefix)) != 0) continue;
    const char *rel = f->path + strlen(src_prefix);
    if (!rel[0]) continue;
    char dest[1400];
    snprintf(dest, sizeof(dest), "%s/%s", dest_root, rel);
    if (mkdir_for_file(dest) != 0) die("mkdir failed: %s", dest);
    cerco_sb sb;
    sb_init(&sb);
    substitute((const char *)f->data, f->len, name, &sb);
    if (write_file(dest, sb.buf, sb.len) != 0) die("write failed: %s", dest);
    sb_free(&sb);
    printf("  create %s\n", rel);
  }
}

int cmd_new(int argc, char **argv) {
  if (argc < 1 || !argv[0][0]) {
    die("usage: cerco new <name>");
  }
  const char *name = argv[0];
  /* validate name: identifier-ish */
  for (const char *p = name; *p; p++) {
    if (!(( *p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') ||
          (*p >= 'A' && *p <= 'Z') || *p == '-' || *p == '_')) {
      die("invalid project name '%s'", name);
    }
  }
  if (is_dir(name)) die("directory '%s' already exists", name);

  printf("creating %s...\n", name);
  if (mkdirs(name) != 0) die("cannot create %s", name);

  /* copy template files: they live under templates/default/<path> in bundle */
  char prefix[64];
  snprintf(prefix, sizeof(prefix), "templates/default/");
  copy_template_dir(prefix, name, name);

  /* ensure public/ + dist dirs exist even without template entries */
  char p[1200];
  snprintf(p, sizeof(p), "%s/public", name);
  mkdirs(p);
  snprintf(p, sizeof(p), "%s/.gitignore", name);
  if (!file_exists(p)) {
    write_file(p, ".cerco/\ndist/\n", 13);
  }

  printf("\nnext steps:\n  cd %s\n  cerco dev\n", name);
  return 0;
}
