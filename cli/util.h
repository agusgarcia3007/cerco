/* cerco CLI utilities */
#ifndef CERCO_CLI_UTIL_H
#define CERCO_CLI_UTIL_H

#include "sb.h"
#include <stddef.h>
#include <stdint.h>

/* run a subprocess (argv NULL-terminated). Returns exit code (>=0) or -1 on
 * spawn error. If out_fd >= 0 and out_path != NULL, redirect stdout/stderr:
 * when capture != 0, capture combined output into *out (malloc'd). */
int run_cmd(char *const argv[], const char *cwd, int capture, char **out,
            size_t *out_len);

int write_file(const char *path, const void *data, size_t len);
char *read_file(const char *path, size_t *len); /* malloc'd, NUL-terminated */
int mtime_ms(const char *path, int64_t *out);
int file_exists(const char *path);
int is_dir(const char *path);
int mkdirs(const char *path); /* recursive */
/* recursive walk: calls cb(rel_path, full_path, is_dir, user); root prefix stripped */
int walk_dir(const char *root, const char *rel, void (*cb)(const char *rel,
             const char *full, int is_dir, void *user), void *user);
int rm_recursive(const char *path);
void path_join(cerco_sb *b, const char *a, const char *b2);
const char *path_basename(const char *p);
/* mkdir for dirname portion of path */
int mkdir_for_file(const char *path);
void die(const char *fmt, ...);
/* trim helpers for toml parsing */
char *trim(char *s);

#endif
