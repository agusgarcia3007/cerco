/* cerco CLI entry point */
#ifndef CERCO_CLI_MAIN_H
#define CERCO_CLI_MAIN_H

#include <stdio.h>

#define CERCO_VERSION "0.1.0-alpha"
#define CERCO_APP_NAME_MAX 128

typedef struct {
  char name[CERCO_APP_NAME_MAX];
  char version[64];
  char tailwind_version[64];
  char root[1024]; /* project root, cwd by default */
} cerco_project;

/* locate cerco.toml upward from cwd; fills proj; 0 ok */
int project_load(cerco_project *proj, const char *explicit_root);

/* commands */
int cmd_new(int argc, char **argv);
int cmd_build(cerco_project *proj, int argc, char **argv);
int cmd_dev(cerco_project *proj, int argc, char **argv);
int cmd_clean(cerco_project *proj);
int cmd_doctor(void);

/* shared internals */
typedef struct cerco_sdk cerco_sdk;
int sdk_ensure(cerco_project *proj, char *out_dir, size_t out_cap);

int tailwind_ensure(cerco_project *proj, char *out, size_t cap);
int tailwind_run(cerco_project *proj, const char *tw_bin, const char *cwd,
                 const char *in, const char *out_css, int watch);

extern int g_verbose;

#endif
