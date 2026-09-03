# concurrency model

```
             network
                |
         libuv event loop      <- only this thread touches sockets
                |
        parse / route / static
                |
         bounded job ring      <- CERCO_WORK_QUEUE (default 1024)
                |
     +-----+-----+-----+-----+
     v     v     v     v     v
   worker worker worker worker  <- uv threads, 1 MiB stacks
     \     |     |     /
      bounded completion ring
                |
         uv_async_t wakes loop
                |
             socket write
```

Rules:

1. **Only the event loop touches libuv handles/sockets.** Workers receive a
   request context (arena-backed), run the handler, and push the completion.
2. **The job ring is bounded.** If full: HTTP 503 (backpressure), never grow.
3. **The completion ring is bounded** by the job ring size; completions can
   never exceed in-flight jobs.
4. **Workers never block the loop.** They may call `cerco_cancelled(r)` at
   loop boundaries — cancelled work is discarded at completion.
5. **Static assets and reserved endpoints never enter the pool** (event loop
   inline, zero worker contention).
6. **Worker count is bounded** (`CERCO_WORKERS`, default min(ncpu, 4), max 64);
   stacks are 1 MiB via `uv_thread_create_ex`.

## timeouts (1s sweep timer, no per-connection timers)

| Phase | Limit |
|---|---|
| headers | CERCO_HEADER_TIMEOUT_MS (15s) — slowloris protection |
| body | CERCO_BODY_TIMEOUT_MS (30s) |
| worker in flight | CERCO_REQUEST_TIMEOUT_MS (60s) -> cancelled + dropped |
| keep-alive idle | CERCO_KEEPALIVE_TIMEOUT_MS (75s) |
| graceful shutdown | CERCO_SHUTDOWN_TIMEOUT_MS (10s) -> forced stop |

## connection lifecycle

- accept: linked + counted; over `CERCO_MAX_CONNECTIONS` -> 503 + close
- request: `on_message_complete` pauses the parser and dispatches
  (reserved endpoints -> routes -> static -> 404)
- response: staged in the request arena; written from the event loop
- close: hard close (EOF/errors) or graceful half-close (FIN + short drain)
  so error responses are delivered before any RST

## cancellation

Client disconnect or timeout sets an atomic cancel flag on the request. The
worker sees it via `cerco_cancelled()`; the completion is discarded safely and
the connection torn down. Workers are never preempted.

## shutdown

SIGTERM/SIGINT: stop accepting, close idle connections, cancel in-flight work;
when connections and queue drain (or the hard timeout hits) the loop stops,
workers join, and the process exits.

## single process

The alpha runs one process, one loop, bounded workers. `cerco serve
--processes N` (SO_REUSEPORT) is future work; the internals are structured
for it.
