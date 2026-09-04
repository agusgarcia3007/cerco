#define _POSIX_C_SOURCE 200809L
#include "main.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* cerco dev: build (debug), run the server as a child process, watch files,
 * rebuild incrementally, restart the server and ping browsers via SSE. */

static pid_t g_child = 0;

/* try to bind the port ourselves: if that fails, the upcoming server
 * would fail too (OrbStack, another dev server, a stale process...) */
static int port_in_use(int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return 0;
  /* SO_REUSEADDR mirrors what the server itself does (libuv sets it), so
   * lingering TIME_WAIT sockets don't read as "in use"; an active listener
   * still fails the bind */
  int on = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(0x7f000001UL); /* 127.0.0.1 */
  addr.sin_port = htons((uint16_t)port);
  int in_use = bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0;
  close(fd);
  return in_use;
}

static long long dev_now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void spawn_server(cerco_project *proj, int port) {
  char bin[1400];
  snprintf(bin, sizeof(bin), "%s/dist/%s", proj->root, proj->name);
  char reload_file[1300];
  snprintf(reload_file, sizeof(reload_file), "%s/.cerco/reload_signal", proj->root);

  char portbuf[16];
  snprintf(portbuf, sizeof(portbuf), "%d", port);
  char dirdist[1300];
  snprintf(dirdist, sizeof(dirdist), "%s/dist", proj->root);

  pid_t pid = fork();
  if (pid == 0) {
    setenv("CERCO_DEV", "1", 1);
    setenv("CERCO_DIST_DIR", dirdist, 1);
    setenv("CERCO_DEV_RELOAD_FILE", reload_file, 1);
    setenv("PORT", portbuf, 1);
    setenv("HOST", "127.0.0.1", 1);
    execl(bin, bin, (char *)NULL);
    _exit(127);
  }
  g_child = pid;
}

static void stop_server(void) {
  if (g_child <= 0) return;
  kill(g_child, SIGTERM);
  for (int i = 0; i < 20; i++) {
    int status;
    pid_t r = waitpid(g_child, &status, WNOHANG);
    if (r == g_child || (r < 0 && errno == ECHILD)) { g_child = 0; return; }
    usleep(100 * 1000);
  }
  kill(g_child, SIGKILL);
  waitpid(g_child, NULL, 0);
  g_child = 0;
}

/* snapshot of watched files: path + mtime, hashed */
typedef struct {
  char path[1024];
  int64_t mtime;
} watch_entry;

static watch_entry g_watch[4096];
static int g_watch_n = 0;

static void watch_cb(const char *rel, const char *full, int is_dir, void *user) {
  (void)user;
  if (g_watch_n >= 4096) return;
  int64_t mt = 0;
  if (is_dir) {
    /* watch dirs via mtime too (file creation) */
    mtime_ms(full, &mt);
  } else {
    mtime_ms(full, &mt);
  }
  watch_entry *e = &g_watch[g_watch_n++];
  snprintf(e->path, sizeof(e->path), "%s", full);
  e->mtime = mt;
}

static int watch_scan(cerco_project *proj) {
  int old_n = g_watch_n;
  watch_entry old[4096];
  memcpy(old, g_watch, sizeof(watch_entry) * (size_t)old_n);
  g_watch_n = 0;
  char src[1300];
  snprintf(src, sizeof(src), "%s/src", proj->root);
  walk_dir(src, "", watch_cb, NULL);
  char toml[1300];
  snprintf(toml, sizeof(toml), "%s/cerco.toml", proj->root);
  if (file_exists(toml)) {
    int64_t mt;
    mtime_ms(toml, &mt);
    watch_entry *e = &g_watch[g_watch_n++];
    snprintf(e->path, sizeof(e->path), "%s", toml);
    e->mtime = mt;
  }

  int changed = 0;
  if (old_n != g_watch_n) changed = 1;
  else {
    for (int i = 0; i < g_watch_n; i++) {
      if (old[i].mtime != g_watch[i].mtime ||
          strcmp(old[i].path, g_watch[i].path) != 0) {
        changed = 1;
        break;
      }
    }
  }
  return changed;
}

/* 1 (and reaps) when the child server process has exited */
static int child_dead(void) {
  if (g_child <= 0) return 0;
  int status;
  pid_t r = waitpid(g_child, &status, WNOHANG);
  if (r == g_child || (r < 0 && errno == ECHILD)) {
    g_child = 0;
    return 1;
  }
  return 0;
}

static void touch_reload(cerco_project *proj) {
  char f[1300];
  snprintf(f, sizeof(f), "%s/.cerco/reload_signal", proj->root);
  FILE *fp = fopen(f, "w");
  if (fp) {
    fprintf(fp, "%lld\n", dev_now_ms());
    fclose(fp);
  }
}

/* poll until the child server accepts connections (or dies): reports "ready"
 * as soon as the server is actually listening instead of after a fixed nap.
 * 0 = up, -1 = dead or timed out */
static int wait_server_up(int port, int timeout_ms) {
  for (int waited = 0; waited < timeout_ms; waited += 20) {
    if (child_dead()) return -1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd >= 0) {
      struct sockaddr_in addr;
      memset(&addr, 0, sizeof(addr));
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(0x7f000001UL);
      addr.sin_port = htons((uint16_t)port);
      int up = connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0;
      close(fd);
      if (up) return 0;
    }
    usleep(20 * 1000);
  }
  return -1;
}

int cmd_dev(cerco_project *proj, int argc, char **argv) {
  setvbuf(stdout, NULL, _IOLBF, 0); /* watch/rebuild logs visible when piped */
  int port = 3000;
  for (int i = 0; i < argc; i++) {
    if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
  }

  if (port_in_use(port)) {
    fprintf(stderr,
            "cerco dev: port %d is already in use on this machine.\n"
            "  something else answered there (OrbStack? another server?) —\n"
            "  see: lsof -nP -iTCP:%d -sTCP:LISTEN\n"
            "  or pick another port: cerco dev --port %d\n",
            port, port, port + 1);
    return 1;
  }

  char gen[1300];
  snprintf(gen, sizeof(gen), "%s/.cerco", proj->root);
  mkdirs(gen);
  char reload_file[1300];
  snprintf(reload_file, sizeof(reload_file), "%s/.cerco/reload_signal", proj->root);
  if (!file_exists(reload_file)) write_file(reload_file, "0\n", 2);

  /* initial build */
  cerco_project p2 = *proj;
  int rc;
  {
    char arg0[] = "--debug";
    char arg1[] = "--dev-assets";
    char *bargv[3] = { arg0, arg1, NULL };
    rc = cmd_build(&p2, 2, bargv);
  }
  if (rc != 0) {
    fprintf(stderr, "\ncerco dev: initial build failed; waiting for changes...\n");
  }

  /* tailwind watch child */
  pid_t tw_child = 0;
  {
    char twbin[1200];
    char css_in[1400];
    snprintf(css_in, sizeof(css_in), "%s/src/styles.css", proj->root);
    if (file_exists(css_in) && tailwind_ensure(proj, twbin, sizeof(twbin)) == 0) {
      char css_out[1400];
      snprintf(css_out, sizeof(css_out), "%s/dist/assets/styles.css", proj->root);
      mkdir_for_file(css_out);
      pid_t pid = fork();
      if (pid == 0) {
        chdir(proj->root);
        execl(twbin, twbin, "-i", "src/styles.css", "-o",
              "dist/assets/styles.css", "--watch", (char *)NULL);
        _exit(127);
      }
      tw_child = pid;
    }
  }

  if (rc == 0) {
    spawn_server(proj, port);
    if (wait_server_up(port, 5000) != 0) {
      fprintf(stderr, "cerco dev: server exited during startup (see error above)\n");
      if (tw_child) kill(tw_child, SIGTERM);
      return 1;
    }
    printf("\nCerco dev\n  http://localhost:%d\n\n", port);
  }

  /* watch loop */
  watch_scan(proj); /* baseline */
  for (;;) {
    usleep(400 * 1000);
    if (child_dead()) {
      fprintf(stderr, "cerco dev: server exited unexpectedly\n");
      if (tw_child) kill(tw_child, SIGTERM);
      return 1;
    }
    if (!watch_scan(proj)) continue;
    printf("\nchange detected — rebuilding...\n");
    int64_t old_bin_mtime = 0, new_bin_mtime = 0;
    char bin[1400];
    snprintf(bin, sizeof(bin), "%s/dist/%s", proj->root, proj->name);
    mtime_ms(bin, &old_bin_mtime);

    char arg0[] = "--debug";
    char arg1[] = "--dev-assets";
    char *bargv[3] = { arg0, arg1, NULL };
    int brc = cmd_build(&p2, 2, bargv);
    if (brc != 0) {
      fprintf(stderr, "build failed — keeping previous server running\n");
      continue;
    }
    mtime_ms(bin, &new_bin_mtime);
    if (new_bin_mtime != old_bin_mtime) {
      printf("server rebuilt — restarting\n");
      stop_server();
      spawn_server(proj, port);
      if (wait_server_up(port, 5000) != 0) {
        fprintf(stderr, "cerco dev: server exited during restart (see error above)\n");
        if (tw_child) kill(tw_child, SIGTERM);
        return 1;
      }
    }
    touch_reload(proj);
    printf("ready: http://localhost:%d\n", port);
  }
  stop_server();
  if (tw_child) kill(tw_child, SIGTERM);
  return 0;
}

int cmd_clean(cerco_project *proj) {
  char p[1300];
  snprintf(p, sizeof(p), "%s/.cerco", proj->root);
  rm_recursive(p);
  snprintf(p, sizeof(p), "%s/dist", proj->root);
  rm_recursive(p);
  printf("cleaned .cerco and dist\n");
  return 0;
}
