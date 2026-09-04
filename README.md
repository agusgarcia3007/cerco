# cerco

> One language. One compiler. One native server. One tiny wasm client.

cerco is a full-stack web framework for **C**. You write C; cerco compiles your
application into a **native server binary** (libuv + llhttp) and a **tiny
WebAssembly client** (signals, events, hydration). A minimal framework-owned
JavaScript host (~250 lines) bridges the wasm client to the browser. No Node,
no npm, no Vite, no JavaScript framework.

```
cerco new my-app
cd my-app
cerco dev          # http://localhost:3000, live reload, Tailwind v4
cerco build        # single-file release binary: dist/my-app
./dist/my-app      # scp it anywhere; HOST/PORT via env
```

## what a route looks like

```c
/* src/routes/pokemon/[name].c — one file, every name */
#include <cerco.h>

CERCO_ROUTE {
  const char *name = cerco_param(r, "name");
  if (!valid_slug(name)) { cerco_status(r, 404); /* ...not-found... */ return; }

  cerco_title(r, name); /* this page's <title>, written into the layout */
  cerco_tag(r, "h1", CERCO_CLASS("text-4xl font-semibold capitalize")) {
    cerco_text(r, name); /* escaped, always */
  }
}
```

```c
/* src/components/counter.c — client side, compiled to wasm */
#include <cerco_client.h>

CERCO_CLIENT_COMPONENT(counter) {
  int32_t root = cerco_root_node(cerco_root);
  cerco_sig *count = cerco_signal_new(cerco_json_int(cerco_props, "start", 0));
  cerco_bind_text(cerco_query(root, "[data-cerco-b=value]"), count);
  cerco_on(cerco_query(root, "[data-cerco-b=inc]"), "click", on_inc, count);
}
```

Client components can also talk to the network directly. `cerco_http_get`
delivers the body whole or not at all — never a silently truncated one — and
NUL-terminates it, so a JSON response can be scanned as a C string:

```c
static void on_list(int status, const uint8_t *body, int32_t len, void *user) {
  if (status != 200) return;                    /* 0 = network, -1 = too large */
  const char *results = strstr((const char *)body, "\"results\"");
  /* ... build nodes with cerco_create / cerco_set_attr / cerco_set_text ... */
}

cerco_http_get("https://pokeapi.co/api/v2/pokemon?limit=151", on_list, 0);
```

## install

Requirements: **clang (LLVM 14+)**, **lld / wasm-ld**, **llvm-ar**.
The release runtime needs none of them — only the built binary.

```bash
curl -fsSL https://raw.githubusercontent.com/agusgarcia3007/cerco/main/scripts/install.sh | sh
```

The script clones the repo to `~/.cerco/src`, builds the CLI and installs it
to `~/.local/bin/cerco`, installing the toolchain first if needed
(Homebrew on macOS, apt/dnf/pacman on Linux). Prefer doing it by hand:

```bash
# macOS
brew install llvm lld
# Debian/Ubuntu
apt install clang lld llvm

git clone https://github.com/agusgarcia3007/cerco.git cerco && cd cerco
make            # builds the cerco CLI (build/cerco)
make install    # optional: ~/.local/bin/cerco
cerco doctor    # verify the toolchain
```

Tailwind is **not** a prerequisite: the CLI downloads the pinned standalone
binary (SHA-256 verified) into `~/.cerco/tools/` on first use. Set
`CERCO_SKIP_TAILWIND=1` to skip it entirely.

Builds are cached: the vendored runtime (libuv, llhttp, framework server)
compiles once into `~/.cerco/cache/` and is shared by every project; app
sources compile in parallel and unchanged code is skipped, so `cerco dev`
starts in a fraction of a second.

## project layout

```
src/routes/        file-based routing     index.c -> /, users/[id].c -> /users/:id
src/routes/layout.c  root layout; each directory may add its own layout.c
src/routes/*.post.c   method by file suffix: .get .post .put .patch .delete
src/routes/404.c      custom not-found page
src/components/    client components (CERCO_CLIENT_COMPONENT), compiled to wasm
src/server/        server-only code (never enters the wasm binary)
src/server/functions.x  server-function declarations (X-macros)
src/shared/        compiled into BOTH targets
src/styles.css     tailwind v4 entry (@import "tailwindcss";)
public/            static files, copied into dist/
```

**Client/server split is structural and auditable:** `src/routes/**` and
`src/server/**` are excluded from the wasm target. Secrets and database code
stay on the server.

## server functions

```c
/* src/server/functions.x — declaration */
CERCO_SF(1, add, CERCO_I32, CERCO_I32, CERCO_I32)

/* src/server/functions.c — implementation */
int32_t sf_add(cerco_sf_ctx *ctx, int32_t a, int32_t b) {
  if (a < 0) { cerco_sf_fail(ctx, "no negatives"); return 0; }
  return a + b;
}
```

The client calls it from component code with a callback (never blocks):

```c
cerco_sf_add(2, 3, on_result, user_data);
```

Wire format is an explicit binary codec (LE integers, typed values) — no
struct memcpy across the native/wasm boundary. Only registered functions are
callable; method and content-type are validated.

## configuration (runtime, environment)

| Variable | Default | Meaning |
|---|---|---|
| HOST / PORT | 0.0.0.0 / 3000 | bind address |
| CERCO_WORKERS | min(ncpu, 4) | worker threads (bounded) |
| CERCO_WORK_QUEUE | 1024 | bounded job ring; overflow -> 503 |
| CERCO_MAX_CONNECTIONS | 10000 | accept cap; overflow -> 503 |
| CERCO_MAX_BODY | 2 MiB | request body cap -> 413 |
| CERCO_LOG_LEVEL | info | trace..error |
| CERCO_TRUST_PROXY | 0 | honor X-Forwarded-* |
| CERCO_REQUEST_MEMORY_LIMIT | 2 MiB | per-request arena cap |
| CERCO_STATS | 0 | enable /__cerco/stats |
| CERCO_HEADER/BODY/REQUEST/KEEPALIVE_TIMEOUT_MS | 15s/30s/60s/75s | timeouts |

`/__cerco/health` is always available (cheap "ok").

## what cerco does NOT do (alpha)

No HTTP/2 or 3, no TLS (put Caddy/Nginx/Cloudflare in front), no Windows
toolchain, no HMR with state preservation (correct live reload only), no ORM,
no package manager, no plugins. See `docs/architecture.md`.

## docs

- [docs/architecture.md](docs/architecture.md) — components and data flow
- [docs/concurrency.md](docs/concurrency.md) — worker pool, backpressure, shutdown
- [docs/memory.md](docs/memory.md) — arenas, pools, wasm heap
- [docs/browser-runtime.md](docs/browser-runtime.md) — host ABI, hydration, signals
- [docs/server-functions.md](docs/server-functions.md) — wire format, codegen
- [docs/deployment.md](docs/deployment.md) — binary, env, Docker, proxies

## development (this repo)

```bash
make              # build the CLI
make test         # unit tests under ASan+UBSan
make test-integration  # HTTP suite against a real server
make test-tsan    # ThreadSanitizer
```
