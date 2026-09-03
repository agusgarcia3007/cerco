# my-app

A [cerco](https://github.com/.../cerco) application.

## develop

```
cerco dev        # http://localhost:3000 with live reload
```

## build

```
cerco build      # single-file release binary in dist/my-app
./dist/my-app    # serves on PORT=3000 HOST=0.0.0.0
```

## layout

```
src/routes/       file-based routing (index.c -> /, [id].c -> /:id)
src/routes/layout.c  root layout (nested layout.c supported per directory)
src/components/   client components (hydrated from wasm, fine-grained signals)
src/server/       server-only code (never compiled into the wasm client)
src/server/functions.x  server function declarations (called from the client)
src/shared/       code compiled into both targets
src/styles.css    tailwind v4 entry (standalone CLI, no node)
public/           static files copied into dist/
```

## configuration (environment)

```
HOST=0.0.0.0  PORT=3000  CERCO_WORKERS  CERCO_WORK_QUEUE  CERCO_MAX_CONNECTIONS
CERCO_MAX_BODY  CERCO_LOG_LEVEL  CERCO_TRUST_PROXY  CERCO_REQUEST_MEMORY_LIMIT
```
