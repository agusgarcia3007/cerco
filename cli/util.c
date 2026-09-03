#define _DARWIN_C_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include <libgen.h>

void die(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "cerco: ");
  vfprintf(stderr, fmt, ap);
  fprintf(stderr, "\n");
  va_end(ap);
  exit(1);
}

char *trim(char *s) {
  while (*s == ' ' || *s == '\t' || *s == '\r') s++;
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' ||
                   s[n - 1] == '\n')) {
    s[--n] = 0;
  }
  return s;
}

int run_cmd(char *const argv[], const char *cwd, int capture, char **out,
            size_t *out_len) {
  int pipefd[2] = {-1, -1};
  if (capture && pipe(pipefd) != 0) return -1;
  pid_t pid = fork();
  if (pid < 0) return -1;
  if (pid == 0) {
    if (cwd && chdir(cwd) != 0) _exit(127);
    if (capture) {
      close(pipefd[0]);
      dup2(pipefd[1], 1);
      dup2(pipefd[1], 2);
      close(pipefd[1]);
    }
    execvp(argv[0], argv);
    _exit(127);
  }
  if (capture) close(pipefd[1]);
  cerco_sb sb;
  sb_init(&sb);
  if (capture) {
    char buf[8192];
    ssize_t n;
    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
      sb_putn(&sb, buf, (size_t)n);
    }
    close(pipefd[0]);
  } else {
    /* drain nothing; child inherits tty */
  }
  int status = 0;
  waitpid(pid, &status, 0);
  if (WIFEXITED(status)) status = WEXITSTATUS(status);
  else status = -1;
  if (capture) {
    if (out) *out = sb.buf ? sb.buf : NULL;
    else free(sb.buf);
    if (out_len) *out_len = sb.len;
  } else {
    free(sb.buf);
  }
  return status;
}

int write_file(const char *path, const void *data, size_t len) {
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  if (len && fwrite(data, 1, len, f) != len) { fclose(f); return -1; }
  fclose(f);
  return 0;
}

char *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz < 0) { fclose(f); return NULL; }
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) { fclose(f); return NULL; }
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { free(buf); fclose(f); return NULL; }
  buf[sz] = 0;
  fclose(f);
  if (len) *len = (size_t)sz;
  return buf;
}

int mtime_ms(const char *path, int64_t *out) {
  struct stat st;
  if (stat(path, &st) != 0) return -1;
#if defined(__APPLE__)
  *out = (int64_t)st.st_mtimespec.tv_sec * 1000 + st.st_mtimespec.tv_nsec / 1000000;
#else
  *out = (int64_t)st.st_mtim.tv_sec * 1000 + st.st_mtim.tv_nsec / 1000000;
#endif
  return 0;
}

int file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int is_dir(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int mkdirs(const char *path) {
  char tmp[1024];
  snprintf(tmp, sizeof(tmp), "%s", path);
  size_t n = strlen(tmp);
  if (n && tmp[n - 1] == '/') tmp[n - 1] = 0;
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = 0;
      if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
      *p = '/';
    }
  }
  if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return -1;
  return 0;
}

int mkdir_for_file(const char *path) {
  char tmp[1024];
  snprintf(tmp, sizeof(tmp), "%s", path);
  char *slash = strrchr(tmp, '/');
  if (!slash) return 0;
  *slash = 0;
  return mkdirs(tmp);
}

int walk_dir(const char *root, const char *rel,
             void (*cb)(const char *rel, const char *full, int is_dir, void *user),
             void *user) {
  char fullpath[1024];
  if (rel[0]) snprintf(fullpath, sizeof(fullpath), "%s/%s", root, rel);
  else snprintf(fullpath, sizeof(fullpath), "%s", root);
  DIR *d = opendir(fullpath);
  if (!d) return -1;
  struct dirent *e;
  char relbuf[1024], childfull[1024];
  while ((e = readdir(d)) != NULL) {
    if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
    if (rel[0]) snprintf(relbuf, sizeof(relbuf), "%s/%s", rel, e->d_name);
    else snprintf(relbuf, sizeof(relbuf), "%s", e->d_name);
    snprintf(childfull, sizeof(childfull), "%s/%s", root, relbuf);
    struct stat st;
    if (stat(childfull, &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      cb(relbuf, childfull, 1, user);
      walk_dir(root, relbuf, cb, user);
    } else if (S_ISREG(st.st_mode)) {
      cb(relbuf, childfull, 0, user);
    }
  }
  closedir(d);
  return 0;
}

int rm_recursive(const char *path) {
  struct stat st;
  if (lstat(path, &st) != 0) return errno == ENOENT ? 0 : -1;
  if (S_ISDIR(st.st_mode)) {
    DIR *d = opendir(path);
    if (!d) return -1;
    struct dirent *e;
    char child[1024];
    while ((e = readdir(d)) != NULL) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
      rm_recursive(child);
    }
    closedir(d);
    return rmdir(path);
  }
  return unlink(path);
}

void path_join(cerco_sb *b, const char *a, const char *b2) {
  sb_puts(b, a);
  if (a[0] && a[strlen(a) - 1] != '/') sb_putc(b, '/');
  sb_puts(b, b2);
}

const char *path_basename(const char *p) {
  const char *slash = strrchr(p, '/');
  return slash ? slash + 1 : p;
}
