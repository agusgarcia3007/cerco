# memory model

## server

- **Request arenas**: every request allocates from an arena owned by its
  connection (first chunk 16 KiB, grows by doubling, hard cap
  `CERCO_REQUEST_MEMORY_LIMIT` = 2 MiB). Arena OOM -> HTTP 500, not process
  death. The arena is reset (not freed) between requests; excess chunks are
  released.
- **Connection objects** come from a pool (256) that keeps a warm arena.
  When the pool is full, the arena is destroyed and the struct freed —
  no unbounded retention.
- **Read buffers** (16 KiB) are pooled (256) and handed to libuv per read.
- **Bounded everywhere**: connections (max_connections), jobs (work_queue),
  completions (= queue size), headers (100 / 32 KiB), URL (8 KiB), body
  (max_body), response headers (64), pending writes (4 MiB before drop).
- **RSS behavior**: freed pages are returned to the OS periodically
  (malloc_zone_pressure_relief on macOS). RSS after load returns to near-idle;
  verified over multiple load rounds (see benchmarks).

## wasm client

- Bump heap after `__heap_base`, grows by pages up to CERCO_MAX_HEAP_PAGES
  (64 = 4 MiB default). Allocation failure returns NULL; commands/allocations
  are bounded and checked.
- Two regions: a **persistent heap** (signals, bindings, component state) and
  a **scratch arena** (64 KiB) reset per event batch — strings for events,
  queries, hydration props live here.
- On navigation: persistent heap rewinds (sticky allocations like fetch
  buffers survive), scratch resets, event table resets. Repeated navigation
  does not grow memory (stress-verified).
- Server-function request bodies are bounded (32 KiB); responses use 4
  bounded slots (32 KiB each). Concurrent SF calls beyond the slots are
  rejected rather than queued.
