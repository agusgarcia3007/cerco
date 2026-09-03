#define _POSIX_C_SOURCE 200809L
#include "main.h"
#include "util.h"
#include "bundle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* check that a tool exists and optionally satisfies a minimum major version */
static int check_tool(const char *pretty, const char *env, const char *fallback[],
                      const char *args, int min_major, char *out, size_t out_cap) {
  char *const argv_try[8] = {0};
  char cmd[256];
  const char *bin = getenv(env);
  if (bin && bin[0]) {
    snprintf(cmd, sizeof(cmd), "%s", bin);
  } else if (fallback) {
    /* try each fallback, then bare PATH lookup */
    for (int i = 0; fallback[i]; i++) {
      if (file_exists(fallback[i])) {
        snprintf(cmd, sizeof(cmd), "%s", fallback[i]);
        goto found;
      }
    }
    snprintf(cmd, sizeof(cmd), "%s", pretty);
  } else {
    snprintf(cmd, sizeof(cmd), "%s", pretty);
  }
found:
  (void)argv_try;
  size_t olen = 0;
  char *captured = NULL;
  char argvv[4][256];
  snprintf(argvv[0], 256, "%s", cmd);
  int n = 1;
  /* split args on spaces (simple fixed set: "--version") */
  if (args && args[0]) {
    char tmp[128];
    snprintf(tmp, sizeof(tmp), "%s", args);
    char *tok = strtok(tmp, " ");
    while (tok && n < 3) {
      snprintf(argvv[n], 256, "%s", tok);
      n++;
      tok = strtok(NULL, " ");
    }
  }
  char *argvp[4] = { argvv[0], argvv[1], argvv[2], NULL };
  int rc = run_cmd(argvp, NULL, 1, &captured, &olen);
  if (rc != 0) {
    printf("  [ ] %-12s not found (tried %s)\n", pretty, cmd);
    return 0;
  }
  int major = 0;
  if (captured) {
    /* parse first number in output */
    for (const char *p = captured; *p; p++) {
      if (*p >= '0' && *p <= '9') {
        major = (int)strtol(p, NULL, 10);
        break;
      }
    }
  }
  int ok = (min_major <= 0) || (major >= min_major);
  printf("  [%c] %-12s %s%s\n", ok ? 'x' : ' ', pretty, cmd,
         ok ? "" : " (too old)");
  if (out && ok) snprintf(out, out_cap, "%s", cmd);
  free(captured);
  return ok;
}

int cmd_doctor(void) {
  printf("cerco %s — toolchain check\n", CERCO_VERSION);
  printf("platform: %s (%s)\n", CERCO_PLATFORM, CERCO_ARCH);
  int ok = 1;
  char clang_path[512] = {0};
  char lld_path[512] = {0};
  char ar_path[512] = {0};

  ok &= check_tool("clang", "CERCO_CLANG",
                   (const char *[]){ "/opt/homebrew/opt/llvm/bin/clang",
                                     "/usr/local/opt/llvm/bin/clang", NULL },
                   "--version", 14, clang_path, sizeof(clang_path));
  ok &= check_tool("wasm-ld", "CERCO_WASM_LD",
                   (const char *[]){ "/opt/homebrew/opt/lld/bin/wasm-ld",
                                     "/usr/local/opt/lld/bin/wasm-ld", NULL },
                   "--version", 14, lld_path, sizeof(lld_path));
  ok &= check_tool("llvm-ar", "CERCO_AR",
                   (const char *[]){ "/opt/homebrew/opt/llvm/bin/llvm-ar", NULL },
                   "--version", 14, ar_path, sizeof(ar_path));

  /* tailwind is optional (fetched on demand) */
  const char *tw = getenv("HOME");
  char twprobe[1024];
  snprintf(twprobe, sizeof(twprobe), "%s/.cerco/tools/tailwind", tw ? tw : ".");
  if (is_dir(twprobe)) {
    printf("  [x] %-12s cached at %s\n", "tailwind", twprobe);
  } else {
    printf("  [x] %-12s will download on first dev/build (%s)\n", "tailwind", twprobe);
  }

  if (!ok) {
    printf("\nmissing tools. install with:\n");
    printf("  macOS:   brew install llvm lld\n");
    printf("  Linux:   apt install clang lld llvm   (or llvm.sh)\n");
    return 1;
  }
  printf("\nall good — cerco is ready.\n");
  return 0;
}
