# deployment

## the binary

`cerco build` produces `dist/<name>`: a single static-ish native binary with
all assets embedded (app.wasm, host.js, styles.css, public files). No runtime
dependencies beyond libc/libpthread. No cerco toolchain, compiler, Node, or
source files needed on the host.

```bash
cerco build
scp dist/my-app server:/srv/
ssh server 'HOST=0.0.0.0 PORT=3000 /srv/my-app'
```

`cerco build --debug` keeps symbols (-O1 -g) and still embeds assets.

## environment

```
HOST=0.0.0.0   PORT=3000           bind
CERCO_WORKERS=4                    worker threads
CERCO_WORK_QUEUE=1024              job ring size (503 when full)
CERCO_MAX_CONNECTIONS=10000        accept cap
CERCO_MAX_BODY=2097152             request body bytes
CERCO_LOG_LEVEL=info               trace|debug|info|warn|error
CERCO_STATS=1                      expose /__cerco/stats
CERCO_TRUST_PROXY=0                honor X-Forwarded-For
CERCO_REQUEST_MEMORY_LIMIT=2097152 per-request arena cap
```

## health / stats

- `GET /__cerco/health` — always on, returns "ok"
- `GET /__cerco/stats` — when CERCO_STATS=1: connections, requests, queue,
  status counters, event-loop lag, arena high-water

## TLS / HTTP2

Out of scope for the alpha. Put a proxy in front:

```
# Caddy
example.com {
  reverse_proxy 127.0.0.1:3000
}
```

Nginx, Traefik, Cloudflare, or any LB works the same way (HTTP/1.1 upstream).

## docker

The starter template ships a two-stage Dockerfile:

```bash
docker build -t my-app .     # requires the cerco CLI binary in context
docker run -p 3000:3000 my-app
```

Runtime stage: debian-slim, non-root user, HEALTHCHECK on /__cerco/health.

## graceful shutdown

SIGTERM stops the accept loop, closes idle connections and drains in-flight
requests up to CERCO_SHUTDOWN_TIMEOUT_MS (default 10s), then exits. Works
with docker stop / systemd / kubelet defaults.
