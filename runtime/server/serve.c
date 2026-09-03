/* cerco_serve: config, listener, sweep timer, signals, graceful shutdown. */
#include "internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <sys/stat.h>
#if defined(__APPLE__)
#include <malloc/malloc.h>
#endif

static cerco_server g_srv;
static int64_t g_shutdown_started = 0;

/* dev: watch CERCO_DEV_RELOAD_FILE mtime; on change broadcast SSE reload */
static void dev_check_reload_signal(cerco_server *srv) {
  static int64_t last_mtime = 0;
  static int inited = 0;
  const char *path = getenv("CERCO_DEV_RELOAD_FILE");
  if (!path || !path[0]) return;
  struct stat st;
  if (stat(path, &st) != 0) return;
#if defined(__APPLE__)
  int64_t mtime = (int64_t)st.st_mtimespec.tv_sec * 1000 +
                  st.st_mtimespec.tv_nsec / 1000000;
#else
  int64_t mtime = (int64_t)st.st_mtim.tv_sec * 1000 + st.st_mtim.tv_nsec / 1000000;
#endif
  if (!inited) {
    inited = 1;
    last_mtime = mtime;
    return;
  }
  if (mtime != last_mtime) {
    last_mtime = mtime;
    conn_send_sse_reload(srv);
  }
}

static void on_sweep(uv_timer_t *t) {
  cerco_server *srv = (cerco_server *)t->data;
  int64_t now = cerco_now_ms();
  if (srv->sweep_last) {
    int64_t lag = now - srv->sweep_last - 1000;
    if (lag < 0) lag = 0;
    atomic_store(&srv->stats.event_loop_lag_ms, lag);
  }
  srv->sweep_last = now;
  conn_sweep_deadlines(srv);
#if defined(__APPLE__)
  /* return freed pages (freed request arenas/buffers) to the OS */
  static int sweep_count = 0;
  if (++sweep_count % 5 == 0) {
    malloc_zone_pressure_relief(malloc_default_zone(), 0);
  }
#endif
  if (srv->cfg.dev_mode) dev_check_reload_signal(srv);

  if (atomic_load(&srv->shutting_down)) {
    if (srv->n_conns == 0 && atomic_load(&srv->stats.queued_jobs) == 0) {
      uv_stop(&srv->loop);
    } else if (g_shutdown_started &&
               now - g_shutdown_started > (int64_t)srv->cfg.shutdown_timeout_ms) {
      /* hard deadline: force the loop to stop even if connections linger */
      cerco_log(CERCO_LOG_WARN, "shutdown timeout: forcing stop (conns=%ld)",
                srv->n_conns);
      uv_stop(&srv->loop);
    }
  }
}

static void start_shutdown(cerco_server *srv) {
  int expected = 0;
  if (!atomic_compare_exchange_strong(&srv->shutting_down, &expected, 1)) return;
  g_shutdown_started = cerco_now_ms();
  cerco_log(CERCO_LOG_INFO, "shutting down gracefully (timeout %llu ms)",
            (unsigned long long)srv->cfg.shutdown_timeout_ms);
  uv_close((uv_handle_t *)&srv->listener, NULL);
  cerco_conn *c = srv->conns;
  while (c) {
    cerco_conn *next = c->next;
    if (c->phase == CERCO_PHASE_IDLE || c->phase == CERCO_PHASE_HEADERS ||
        c->phase == CERCO_PHASE_BODY) {
      conn_close_and_free(c);
    } else if (c->req) {
      atomic_store(&c->req->cancelled, 1);
      c->force_close = 1;
    }
    c = next;
  }
}

static void on_stop_async(uv_async_t *a) {
  cerco_server *srv = (cerco_server *)a->loop->data;
  start_shutdown(srv);
}

static void on_signal(uv_signal_t *sig, int signum) {
  (void)signum;
  cerco_server *srv = (cerco_server *)sig->data;
  cerco_log(CERCO_LOG_INFO, "signal %d: shutting down", signum);
  start_shutdown(srv);
}

int cerco_serve(const cerco_app *app, int argc, char **argv) {
  (void)argc;
  (void)argv;
  cerco_server *srv = &g_srv;
  memset(srv, 0, sizeof(*srv));
  srv->app = app;
  cerco_http_init();
  cerco_config_from_env(&srv->cfg);
  cerco_log_set_level(srv->cfg.log_level);
  atomic_store(&srv->shutting_down, 0);

  if (router_init(srv) != 0) {
    fprintf(stderr, "cerco: router init failed\n");
    return 1;
  }

  if (uv_loop_init(&srv->loop) != 0) {
    fprintf(stderr, "cerco: loop init failed\n");
    return 1;
  }
  srv->loop.data = srv;

  if (wpool_start(srv, srv->cfg.workers, srv->cfg.work_queue) != 0) {
    fprintf(stderr, "cerco: worker pool init failed\n");
    return 1;
  }
  cerco_log(CERCO_LOG_INFO, "workers: %d (queue %zu)", srv->cfg.workers,
            srv->cfg.work_queue);

  if (uv_async_init(&srv->loop, &srv->stop_async, on_stop_async) != 0) {
    fprintf(stderr, "cerco: async init failed\n");
    return 1;
  }

  if (uv_tcp_init(&srv->loop, &srv->listener) != 0) {
    fprintf(stderr, "cerco: tcp init failed\n");
    return 1;
  }
  srv->listener.data = srv;

  struct sockaddr_in in;
  memset(&in, 0, sizeof(in));
  in.sin_family = AF_INET;
  if (cerco_strcaseeq(srv->cfg.host, "localhost")) {
    in.sin_addr.s_addr = htonl(0x7F000001);
  } else if (uv_ip4_addr(srv->cfg.host, srv->cfg.port, &in) != 0) {
    in.sin_addr.s_addr = htonl(INADDR_ANY);
  }
  int rc = uv_tcp_bind(&srv->listener, (const struct sockaddr *)&in, 0);
  if (rc != 0) {
    fprintf(stderr, "cerco: bind %s:%d failed: %s\n", srv->cfg.host, srv->cfg.port,
            uv_strerror(rc));
    return 1;
  }
  rc = uv_listen((uv_stream_t *)&srv->listener, 1024, cerco_connection_cb);
  if (rc != 0) {
    fprintf(stderr, "cerco: listen failed: %s\n", uv_strerror(rc));
    return 1;
  }

  uv_timer_init(&srv->loop, &srv->sweep_timer);
  srv->sweep_timer.data = srv;
  uv_timer_start(&srv->sweep_timer, on_sweep, 1000, 1000);

  uv_signal_init(&srv->loop, &srv->sig_int);
  uv_signal_init(&srv->loop, &srv->sig_term);
  srv->sig_int.data = srv;
  srv->sig_term.data = srv;
  uv_signal_start(&srv->sig_int, on_signal, SIGINT);
  uv_signal_start(&srv->sig_term, on_signal, SIGTERM);

  printf("cerco %s listening on http://%s:%d\n", app->name ? app->name : "app",
         srv->cfg.host, srv->cfg.port);
  fflush(stdout);

  uv_run(&srv->loop, UV_RUN_DEFAULT);

  wpool_shutdown(srv);
  uv_timer_stop(&srv->sweep_timer);
  uv_close((uv_handle_t *)&srv->sweep_timer, NULL);
  uv_close((uv_handle_t *)&srv->stop_async, NULL);
  uv_close((uv_handle_t *)&srv->sig_int, NULL);
  uv_close((uv_handle_t *)&srv->sig_term, NULL);
  uv_run(&srv->loop, UV_RUN_NOWAIT);
  uv_loop_close(&srv->loop);
  cerco_log(CERCO_LOG_INFO, "bye");
  return 0;
}
