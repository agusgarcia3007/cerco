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
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* cerco dev: build (debug), run the server as a child process, watch files,
 * rebuild incrementally, restart the server and ping browsers via SSE. */

static pid_t g_child = 0;

/* Can we open a TCP connection to this port on the loopback of one family?
 * A live listener accepts; a socket lingering in TIME_WAIT does not, so this
 * never mistakes a just-restarted server for a busy port. */
static int can_connect(int family, int port) {
  int fd = socket(family, SOCK_STREAM, 0);
  if (fd < 0) return 0; /* family unavailable: nothing can be listening on it */
  int ok;
  if (family == AF_INET6) {
    struct sockaddr_in6 a6;
    memset(&a6, 0, sizeof(a6));
    a6.sin6_family = AF_INET6;
    a6.sin6_addr = in6addr_loopback;
    a6.sin6_port = htons((uint16_t)port);
    ok = connect(fd, (struct sockaddr *)&a6, sizeof(a6)) == 0;
  } else {
    struct sockaddr_in a4;
    memset(&a4, 0, sizeof(a4));
    a4.sin_family = AF_INET;
    a4.sin_addr.s_addr = htonl(0x7f000001UL); /* 127.0.0.1 */
    a4.sin_port = htons((uint16_t)port);
    ok = connect(fd, (struct sockaddr *)&a4, sizeof(a4)) == 0;
  }
  close(fd);
  return ok;
}

/* Would the server fail to take the wildcard address of one family?
 * Catches a listener bound to a non-loopback interface, which no loopback
 * connect would find. SO_REUSEADDR mirrors what libuv does, so TIME_WAIT
 * sockets do not read as "in use". */
static int wildcard_taken(int family, int port) {
  int fd = socket(family, SOCK_STREAM, 0);
  if (fd < 0) return 0;
  int on = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
  int busy;
  if (family == AF_INET6) {
    /* test v6 on its own, or the kernel maps it onto the v4 test */
    setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &on, sizeof(on));
    struct sockaddr_in6 a6;
    memset(&a6, 0, sizeof(a6));
    a6.sin6_family = AF_INET6;
    a6.sin6_addr = in6addr_any;
    a6.sin6_port = htons((uint16_t)port);
    busy = bind(fd, (struct sockaddr *)&a6, sizeof(a6)) != 0;
  } else {
    struct sockaddr_in a4;
    memset(&a4, 0, sizeof(a4));
    a4.sin_family = AF_INET;
    a4.sin_addr.s_addr = htonl(INADDR_ANY);
    a4.sin_port = htons((uint16_t)port);
    busy = bind(fd, (struct sockaddr *)&a4, sizeof(a4)) != 0;
  }
  close(fd);
  return busy;
}

/* Is this port already answering, in either family?
 *
 * Both matter, not just the one cerco binds. A server holding only
 * [::1]:3000 leaves 0.0.0.0:3000 bindable, so cerco used to start
 * "successfully" on the same port and then print a localhost URL that
 * resolved to ::1 and reached the other server instead.
 *
 * A bind test alone is not enough either: with SO_REUSEADDR the wildcard
 * binds happily alongside a listener on 127.0.0.1, so ask by connecting. */
static int port_in_use(int port) {
  return can_connect(AF_INET, port) || can_connect(AF_INET6, port) ||
         wildcard_taken(AF_INET, port) || wildcard_taken(AF_INET6, port);
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
    if (can_connect(AF_INET, port)) return 0;
    usleep(20 * 1000);
  }
  return -1;
}

/* Start tailwind's --watch child. Returns its pid, or 0 when tailwind is
 * unavailable (CERCO_SKIP_TAILWIND, download failure, no styles.css). */
static pid_t spawn_tailwind_watch(cerco_project *proj) {
  char twbin[1200], css_in[1400];
  snprintf(css_in, sizeof(css_in), "%s/src/styles.css", proj->root);
  if (!file_exists(css_in)) return 0;
  if (tailwind_ensure(proj, twbin, sizeof(twbin)) != 0) return 0;

  char css_out[1400];
  snprintf(css_out, sizeof(css_out), "%s/dist/assets/styles.css", proj->root);
  mkdir_for_file(css_out);
  pid_t pid = fork();
  if (pid == 0) {
    chdir(proj->root);
    execl(twbin, twbin, "-i", "src/styles.css", "-o", "dist/assets/styles.css",
          "--watch", (char *)NULL);
    _exit(127);
  }
  return pid > 0 ? pid : 0;
}

/* the watcher writes the stylesheet asynchronously; do not announce a URL
 * whose first paint would arrive unstyled */
static void wait_for_css(cerco_project *proj, int timeout_ms) {
  char css_out[1400];
  snprintf(css_out, sizeof(css_out), "%s/dist/assets/styles.css", proj->root);
  for (int waited = 0; waited < timeout_ms; waited += 20) {
    struct stat st;
    if (stat(css_out, &st) == 0 && st.st_size > 0) return;
    usleep(20 * 1000);
  }
}

int cmd_dev(cerco_project *proj, int argc, char **argv) {
  setvbuf(stdout, NULL, _IOLBF, 0); /* watch/rebuild logs visible when piped */
  int port = 3000;
  int port_chosen = 0; /* the user named a port: never move off it silently */
  for (int i = 0; i < argc; i++) {
    if (!strcmp(argv[i], "--port") && i + 1 < argc) {
      port = atoi(argv[++i]);
      port_chosen = 1;
    }
  }

  if (port_in_use(port)) {
    if (port_chosen) {
      fprintf(stderr,
              "cerco dev: port %d is already in use on this machine.\n"
              "  something else is listening there (another dev server?) —\n"
              "  see: lsof -nP -iTCP:%d -sTCP:LISTEN\n",
              port, port);
      return 1;
    }
    /* the default port is a convenience, not a request: step to the next
     * free one rather than refusing to start */
    int found = 0;
    for (int p = port + 1; p < port + 32; p++) {
      if (port_in_use(p)) continue;
      printf("port %d is in use — starting on %d instead\n"
             "  (what has %d: lsof -nP -iTCP:%d -sTCP:LISTEN)\n",
             port, p, port, port);
      port = p;
      found = 1;
      break;
    }
    if (!found) {
      fprintf(stderr,
              "cerco dev: ports %d-%d are all in use on this machine.\n"
              "  pick one yourself: cerco dev --port <port>\n",
              port, port + 31);
      return 1;
    }
  }

  char gen[1300];
  snprintf(gen, sizeof(gen), "%s/.cerco", proj->root);
  mkdirs(gen);
  char reload_file[1300];
  snprintf(reload_file, sizeof(reload_file), "%s/.cerco/reload_signal", proj->root);
  if (!file_exists(reload_file)) write_file(reload_file, "0\n", 2);

  /* Start tailwind before the C build, not after it: its startup costs about
   * a second, and running it alongside the compile keeps that off the clock.
   * It also becomes the only writer of dist/assets/styles.css — the build
   * used to produce the same file the watcher immediately rebuilt. */
  pid_t tw_child = spawn_tailwind_watch(proj);

  cerco_project p2 = *proj;
  int rc;
  char arg0[] = "--debug";
  char arg1[] = "--dev-assets";
  char arg2[] = "--no-css";
  char *bargv[4] = { arg0, arg1, tw_child ? arg2 : NULL, NULL };
  int bargc = tw_child ? 3 : 2;
  rc = cmd_build(&p2, bargc, bargv);
  if (rc != 0) {
    fprintf(stderr, "\ncerco dev: initial build failed; waiting for changes...\n");
  }

  if (rc == 0) {
    spawn_server(proj, port);
    if (wait_server_up(port, 5000) != 0) {
      fprintf(stderr, "cerco dev: server exited during startup (see error above)\n");
      if (tw_child) kill(tw_child, SIGTERM);
      return 1;
    }
    if (tw_child) wait_for_css(proj, 5000);
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

    int brc = cmd_build(&p2, bargc, bargv);
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
