# my-app

A small site built with [cerco](https://github.com/agusgarcia3007/cerco):
one language (C) on the server and, compiled to wasm, in the client.

## develop

```
cerco dev        # http://localhost:3000 with live reload
```

## what is in here

| Route | File | What it shows |
|---|---|---|
| `/` | `src/routes/index.c` | front page, an index of everything else |
| `/blog` | `src/routes/blog/index.c` | list rendered from `src/server/posts.c` |
| `/blog/one-language` etc. | `src/routes/blog/[slug].c` | path parameter, inline 404 for unknown slugs |
| `/guestbook` | `src/routes/guestbook/index.c` | form + entries |
| `POST /guestbook` | `src/routes/guestbook/index.post.c` | `cerco_form`, validation, `cerco_redirect` (303) |
| `/demo` | `src/routes/demo.c` | client component (signals) + server function call |
| anything else | `src/routes/404.c` | custom not-found page |

`src/routes/layout.c` is the shared shell (masthead, nav, footer). Nested
directories can carry their own `layout.c`.

## build

```
cerco build      # single-file release binary in dist/my-app
./dist/my-app    # serves on HOST=0.0.0.0 PORT=3000
```

## layout

```
src/routes/       file-based routing (index.c -> /, [slug].c -> /:param,
                  name.post.c -> POST, [...rest].c -> catch-all)
src/routes/layout.c  root layout (nested layout.c supported per directory)
src/components/   client components (hydrated from wasm, fine-grained signals)
src/server/       server-only code (never compiled into the wasm client)
src/server/functions.x  server function declarations (called from the client)
src/styles.css    tailwind v4 entry (standalone CLI, no node) + theme tokens
public/           static files copied into dist/
```

## configuration (environment)

```
HOST=0.0.0.0  PORT=3000  CERCO_WORKERS  CERCO_WORK_QUEUE  CERCO_MAX_CONNECTIONS
CERCO_MAX_BODY  CERCO_LOG_LEVEL  CERCO_TRUST_PROXY  CERCO_REQUEST_MEMORY_LIMIT
```
