/* Bounded worker pool.
 *
 * - Fixed threads created with uv_thread_create_ex (1 MiB stacks).
 * - Bounded job ring (mutex + condvar). Producers (event loop) never block:
 *   a full ring rejects the job (HTTP 503 backpressure).
 * - Bounded completion ring + uv_async_t wake the event loop.
 * - Only the event loop touches completion contents; workers only push.
 */
#include "internal.h"
#include <string.h>
#include <stdlib.h>

static void worker_main(void *arg);

void wpool_on_async(uv_async_t *a) {
  (void)a;
  wpool_drain_completions((cerco_server *)a->loop->data);
}

int wpool_start(cerco_server *srv, int n_threads, size_t queue_cap) {
  cerco_wpool *p = &srv->pool;
  memset(p, 0, sizeof(*p));
  p->srv = srv;
  atomic_store(&p->shutdown, 0);

  p->job_cap = queue_cap;
  p->jobs = (cerco_job *)calloc(queue_cap, sizeof(cerco_job));
  p->done_cap = queue_cap;
  p->done = (cerco_req **)calloc(queue_cap, sizeof(cerco_req *));
  if (!p->jobs || !p->done) return -1;

  uv_mutex_init(&p->job_mu);
  uv_cond_init(&p->job_cv);
  uv_mutex_init(&p->done_mu);

  if (uv_async_init(&srv->loop, &p->done_async, wpool_on_async) == 0) {
    p->done_async_inited = 1;
    uv_unref((uv_handle_t *)&p->done_async);
  }
  p->threads = (uv_thread_t *)calloc((size_t)n_threads, sizeof(uv_thread_t));
  if (!p->threads) return -1;
  p->n_threads = n_threads;

  uv_thread_options_t opts;
  opts.flags = UV_THREAD_HAS_STACK_SIZE;
  opts.stack_size = 1 * 1024 * 1024;
  for (int i = 0; i < n_threads; i++) {
    if (uv_thread_create_ex(&p->threads[i], &opts, worker_main, p) != 0) {
      p->n_threads = i; /* degrade: fewer workers than requested */
      break;
    }
  }
  return p->n_threads > 0 ? 0 : -1;
}

int wpool_try_submit(cerco_server *srv, cerco_req *req) {
  cerco_wpool *p = &srv->pool;
  int ok = 0;
  uv_mutex_lock(&p->job_mu);
  if (!atomic_load(&p->shutdown) && p->job_count < p->job_cap) {
    p->jobs[p->job_tail].req = req;
    p->job_tail = (p->job_tail + 1) % p->job_cap;
    p->job_count++;
    ok = 1;
  }
  uv_mutex_unlock(&p->job_mu);
  if (ok) {
    atomic_fetch_add(&srv->stats.queued_jobs, 1);
    uv_cond_signal(&p->job_cv);
  }
  return ok ? 0 : -1;
}

static void wpool_push_done(cerco_wpool *p, cerco_req *req) {
  uv_mutex_lock(&p->done_mu);
  /* completion ring is sized to the job ring; total in-flight completions
   * can never exceed outstanding jobs, so this never overflows */
  p->done[p->done_tail] = req;
  p->done_tail = (p->done_tail + 1) % p->done_cap;
  p->done_count++;
  uv_mutex_unlock(&p->done_mu);
  wpool_wake_event_loop(p->srv);
}

void wpool_drain_completions(cerco_server *srv) {
  cerco_wpool *p = &srv->pool;
  for (;;) {
    uv_mutex_lock(&p->done_mu);
    if (p->done_count == 0) {
      uv_mutex_unlock(&p->done_mu);
      return;
    }
    cerco_req *r = p->done[p->done_head];
    p->done_head = (p->done_head + 1) % p->done_cap;
    p->done_count--;
    uv_mutex_unlock(&p->done_mu);

    atomic_fetch_add(&srv->stats.completed_jobs, 1);
    if (atomic_load(&r->cancelled) || !r->conn || r->conn->finalized ||
        r->conn->closing) {
      /* client gone or timed out: discard result safely */
      if (r->conn) conn_maybe_finalize(r->conn);
      continue;
    }
    /* arena high-water mark */
    long total = (long)r->arena->total;
    long hwm = atomic_load(&srv->stats.arena_hwm);
    while (total > hwm && !atomic_compare_exchange_weak(&srv->stats.arena_hwm, &hwm, total)) {}
    /* arena blew the request memory cap -> guarantee a 500 */
    if (cerco_arena_failed(r->arena) && r->status < 500) {
      r->status = 500;
      wbuf_reset(&r->resp);
      wbuf_puts(&r->resp, "request exceeded memory limit\n");
      r->resp_hdrs = NULL;
      r->resp_hdrs_tail = NULL;
      r->resp_hdrs_count = 0;
    } else if (r->resp.len == 0 && r->status < 300) {
      /* handler produced nothing */
      r->status = 500;
      wbuf_puts(&r->resp, "empty response\n");
    }
    dev_inject_live_script(r);
    conn_start_response(r);
  }
}

static void worker_main(void *arg) {
  cerco_wpool *p = (cerco_wpool *)arg;
  cerco_server *srv = p->srv;
  for (;;) {
    uv_mutex_lock(&p->job_mu);
    while (p->job_count == 0 && !atomic_load(&p->shutdown)) {
      uv_cond_wait(&p->job_cv, &p->job_mu);
    }
    if (p->job_count == 0 && atomic_load(&p->shutdown)) {
      uv_mutex_unlock(&p->job_mu);
      return;
    }
    cerco_req *req = p->jobs[p->job_head].req;
    p->job_head = (p->job_head + 1) % p->job_cap;
    p->job_count--;
    uv_mutex_unlock(&p->job_mu);
    atomic_fetch_sub(&srv->stats.queued_jobs, 1);

    if (req->handler) req->handler(req);

    wpool_push_done(p, req);
  }
}

void wpool_shutdown(cerco_server *srv) {
  cerco_wpool *p = &srv->pool;
  atomic_store(&p->shutdown, 1);
  uv_cond_broadcast(&p->job_cv);
  for (int i = 0; i < p->n_threads; i++) {
    uv_thread_join(&p->threads[i]);
  }
  free(p->threads);
  free(p->jobs);
  free(p->done);
}

void wpool_wake_event_loop(cerco_server *srv) {
  cerco_wpool *p = &srv->pool;
  if (p->done_async_inited) uv_async_send(&p->done_async);
}
