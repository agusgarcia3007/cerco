#define _POSIX_C_SOURCE 200809L
#include "main.h"
#include "util.h"
#include "tailwind_manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/utsname.h>
#include "sha256.h"

/* Tailwind standalone CLI, pinned version, hash-verified. Never "latest".
 * Cache: $HOME/.cerco/tools/tailwind/<version>/<platform>_<arch>/tailwindcss */

static const tailwind_entry *manifest_entry(void) {
  const char *platform =
#if defined(__APPLE__)
      "darwin";
#elif defined(__linux__)
      "linux";
#else
      "";
#endif
  struct utsname u;
  uname(&u);
  const char *arch = !strcmp(u.machine, "arm64") ? "arm64"
                     : !strcmp(u.machine, "aarch64") ? "arm64"
                     : !strcmp(u.machine, "x86_64") ? "x64" : "";
  for (unsigned i = 0; i < TAILWIND_MANIFEST_COUNT; i++) {
    if (!strcmp(tailwind_manifest[i].version, TAILWIND_PINNED_VERSION) &&
        !strcmp(tailwind_manifest[i].platform, platform) &&
        !strcmp(tailwind_manifest[i].arch, arch)) {
      return &tailwind_manifest[i];
    }
  }
  return NULL;
}

int tailwind_ensure(cerco_project *proj, char *out, size_t cap) {
  /* respect the value, not just the presence: CERCO_SKIP_TAILWIND=0 asking
   * for tailwind and getting none is a trap worth not setting */
  {
    const char *skip = getenv("CERCO_SKIP_TAILWIND");
    if (skip && skip[0] && strcmp(skip, "0") != 0 &&
        strcmp(skip, "false") != 0 && strcmp(skip, "no") != 0)
      return -1;
  }

  const tailwind_entry *ent = manifest_entry();
  if (!ent) {
    fprintf(stderr, "cerco: no tailwind manifest entry for this platform\n");
    return -1;
  }
  const char *home = getenv("HOME");
  if (!home) return -1;
  char dir[1200], bin[1300], url[512];
  snprintf(dir, sizeof(dir), "%s/.cerco/tools/tailwind/%s", home, ent->version);
  snprintf(bin, sizeof(bin), "%s/%s_%s/tailwindcss", dir, ent->platform, ent->arch);
  snprintf(url, sizeof(url), "%s", ent->url);

  if (!file_exists(bin)) {
    printf("downloading tailwind %s (%s/%s)...\n", ent->version, ent->platform,
           ent->arch);
    if (mkdir_for_file(bin) != 0) return -1;
    char tmp[1400];
    snprintf(tmp, sizeof(tmp), "%s.download", bin);
    /* curl -L -o */
    char *argv[8];
    argv[0] = "curl";
    argv[1] = "-fSL";
    argv[2] = "-o";
    argv[3] = tmp;
    argv[4] = (char *)url;
    argv[5] = NULL;
    int rc = run_cmd(argv, NULL, 0, NULL, NULL);
    if (rc != 0) {
      unlink(tmp);
      fprintf(stderr, "cerco: tailwind download failed ( continuing without css)\n");
      return -1;
    }
    /* verify sha256 BEFORE executing */
    char cmd[2800];
    snprintf(cmd, sizeof(cmd), "sh -c 'shasum -a 256 \"%s\" 2>/dev/null || sha256sum \"%s\"'",
             tmp, tmp);
    /* do it in-process instead: read file, hash */
    size_t flen = 0;
    char *fdata = read_file(tmp, &flen);
    if (!fdata) return -1;
    char hex[65];
    cerco_sha256_hex(fdata, flen, hex);
    free(fdata);
    if (strcmp(hex, ent->sha256) != 0) {
      fprintf(stderr,
              "cerco: tailwind checksum mismatch!\n  expected %s\n  got      %s\n"
              "refusing to run the downloaded binary; removing it.\n",
              ent->sha256, hex);
      unlink(tmp);
      return -1;
    }
    if (rename(tmp, bin) != 0) return -1;
    chmod(bin, 0755);
  }
  snprintf(out, cap, "%s", bin);
  (void)proj;
  return 0;
}

int tailwind_run(cerco_project *proj, const char *tw_bin, const char *cwd,
                 const char *in, const char *out_css, int watch) {
  (void)proj;
  char *argv[8];
  argv[0] = (char *)tw_bin;
  argv[1] = "-i";
  argv[2] = (char *)in;
  argv[3] = "-o";
  argv[4] = (char *)out_css;
  argv[5] = watch ? "--watch" : NULL;
  argv[6] = watch ? "--minify" : NULL;
  argv[7] = NULL;
  int rc = run_cmd(argv, cwd, 0, NULL, NULL);
  return rc;
}
