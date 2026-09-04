#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include "main.h"
#include "util.h"
#include "bundle.h"
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

int g_verbose = 0;

/* ---- project loading ---- */

static void toml_assign(cerco_project *p, const char *key, const char *value) {
  if (!strcmp(key, "name")) {
    snprintf(p->name, sizeof(p->name), "%s", value);
  } else if (!strcmp(key, "version")) {
    snprintf(p->version, sizeof(p->version), "%s", value);
  } else if (!strcmp(key, "tailwind")) {
    snprintf(p->tailwind_version, sizeof(p->tailwind_version), "%s", value);
  }
}

int project_load(cerco_project *proj, const char *explicit_root) {
  memset(proj, 0, sizeof(*proj));
  /* find cerco.toml upward */
  char dir[1024];
  if (explicit_root) {
    snprintf(dir, sizeof(dir), "%s", explicit_root);
  } else {
    if (!getcwd(dir, sizeof(dir))) {
      /* ENOENT here means the shell's working directory was deleted or
       * replaced from elsewhere — the usual cause, and not something the
       * user can guess from "cannot get cwd" */
      if (errno == ENOENT)
        die("the current directory no longer exists (it was deleted or "
            "replaced).\n"
            "       Re-enter it to pick up the new one: cd \"$PWD\"");
      die("cannot read the current directory: %s", strerror(errno));
    }
  }
  char probe[1200];
  for (int depth = 0; depth < 16; depth++) {
    snprintf(probe, sizeof(probe), "%s/cerco.toml", dir);
    if (file_exists(probe)) break;
    char *slash = strrchr(dir, '/');
    if (!slash || slash == dir) {
      if (slash == dir && depth > 0) break;
      die("no cerco.toml found (run inside a cerco project, or use `cerco new`)");
    }
    *slash = 0;
  }
  snprintf(proj->root, sizeof(proj->root), "%s", dir);

  size_t len = 0;
  char *data = read_file(probe, &len);
  if (!data) die("cannot read %s", probe);
  char section[64] = "";
  char *saveptr = NULL;
  for (char *line = strtok_r(data, "\n", &saveptr); line;
       line = strtok_r(NULL, "\n", &saveptr)) {
    char *t = trim(line);
    if (!t[0] || t[0] == '#') continue;
    if (t[0] == '[') {
      char *end = strchr(t, ']');
      if (end) {
        *end = 0;
        snprintf(section, sizeof(section), "%s", trim(t + 1));
      }
      continue;
    }
    char *eq = strchr(t, '=');
    if (!eq) continue;
    *eq = 0;
    char *key = trim(t);
    char *val = trim(eq + 1);
    size_t vlen = strlen(val);
    if (vlen >= 2 && val[0] == '"' && val[vlen - 1] == '"') {
      val[vlen - 1] = 0;
      val++;
    }
    /* strip inline comments on unquoted values */
    char *hash = strchr(val, '#');
    if (hash && val[0] != '"') *hash = 0;
    if (section[0]) {
      char full[128];
      snprintf(full, sizeof(full), "%s.%s", section, key);
      toml_assign(proj, full, val);
    } else {
      toml_assign(proj, key, val);
    }
  }
  free(data);
  if (!proj->name[0]) {
    /* default: directory name */
    const char *base = path_basename(proj->root);
    snprintf(proj->name, sizeof(proj->name), "%s", base[0] ? base : "app");
  }
  if (!proj->tailwind_version[0]) snprintf(proj->tailwind_version, sizeof(proj->tailwind_version), "4.1.16");
  return 0;
}

/* ---- help ---- */

static void print_help(void) {
  printf(
      "cerco — one language, one native server, one tiny wasm client\n"
      "\n"
      "usage: cerco <command>\n"
      "\n"
      "commands:\n"
      "  new <name>       scaffold a new project\n"
      "  dev              run the development server with live reload\n"
      "  build [--debug]  build the release binary into dist/\n"
      "  clean            remove build artifacts (.cerco, dist)\n"
      "  doctor           verify the toolchain\n"
      "  --version        print version\n"
      "  help             show this help\n");
}

int main(int argc, char **argv) {
  if (argc < 2) {
    print_help();
    return 0;
  }
  const char *cmd = argv[1];
  if (!strcmp(cmd, "--version") || !strcmp(cmd, "version")) {
    printf("cerco %s\n", CERCO_VERSION);
    return 0;
  }
  if (!strcmp(cmd, "help") || !strcmp(cmd, "--help")) {
    print_help();
    return 0;
  }
  if (!strcmp(cmd, "-v") || !strcmp(cmd, "--verbose")) {
    g_verbose = 1;
    argc--;
    argv++;
    if (argc < 2) {
      print_help();
      return 0;
    }
    cmd = argv[1];
  }

  if (!strcmp(cmd, "new")) return cmd_new(argc - 2, argv + 2);
  if (!strcmp(cmd, "doctor")) return cmd_doctor();

  cerco_project proj;
  project_load(&proj, NULL);

  if (!strcmp(cmd, "build")) return cmd_build(&proj, argc - 2, argv + 2);
  if (!strcmp(cmd, "dev")) return cmd_dev(&proj, argc - 2, argv + 2);
  if (!strcmp(cmd, "clean")) return cmd_clean(&proj);

  fprintf(stderr, "cerco: unknown command '%s'\n", cmd);
  print_help();
  return 1;
}
