# cerco — architecture

```
                 application C
                      |
               cerco build system
                 /           \
                /             \
               v               v
       native server       browser WASM
        (libuv+llhttp)     (signals, cmd buffer)
               |               |
               |          tiny JS host (~250 lines)
               |               |
               +---- browser --+
```

## build pipeline (cerco build)

```
discover project (cerco.toml)
  -> extract SDK into .cerco/sdk (content-hashed; CLI embeds headers,
     runtime sources, templates, prebuilt libuv/llhttp)
  -> scan src/routes           -> .cerco/gen/routes.c (+ layout chains,
                                  page markers <!--cerco:page-->)
  -> scan src/server/functions.x -> .cerco/gen/sf_server.c, sf_client.c/.h
  -> scan src/components       -> .cerco/gen/components.c (registry)
  -> compile wasm target       -> dist/assets/app.wasm  (clang --target=wasm32,
                                  -nostdlib, --gc-sections, freestanding)
  -> host.js + public/ -> dist/
  -> tailwind (standalone, pinned, sha256-verified) -> dist/assets/styles.css
  -> generate .cerco/gen/assets.c (embeds dist assets + public files)
  -> compile + link native server -> dist/<name>
```

Release builds embed all assets in the binary: one file deployable.

## routing

File-based. The CLI generates a static table `{method, path, fn}` at build
time; the server parses it into a segment tree at startup (router_init).
`:param` captures one segment; `*name` captures the rest. `HEAD` is served by
`GET` handlers (body suppressed). `OPTIONS` returns 204 + `Allow`. Unknown
path -> custom `/__cerco/404` route or built-in 404.

Layouts: each directory under src/routes may define layout.c. The generated
dispatch wraps each page with its layout chain (root -> leaf) and page
markers used by client-side navigation.

## SSR

Handlers write HTML incrementally into an arena-backed response buffer
(cerco_tag/cerco_text/cerco_raw). Text and attribute values are HTML-escaped
by default; `cerco_raw` is the only unescaped path. The response is staged in
the request arena and written by the event loop when the worker completes.

Static assets (release: embedded table; dev: dist/ on disk with traversal
protection) are served inline on the event loop — they never enter the worker
pool. ETag (sha256) + If-None-Match -> 304.

## client runtime

- signals with fine-grained subscribers (no VDOM, no diffing)
- DOM command buffer flushed to the host in batches (one wasm->JS call per
  batch): CREATE/SET_TEXT/SET_ATTR/ADD_EVENT/...
- hydration: the host scans `[data-cerco]` roots, assigns node ids, passes
  component name + props; the wasm mount function binds signals to existing
  SSR DOM (no re-render)
- navigation: fetch page, swap `<!--cerco:page-->` region via one innerHTML
  command, re-hydrate; popstate and link interception in the host
- wasm heap: bump allocator, capped at CERCO_MAX_HEAP_PAGES (64 = 4 MiB);
  rewinds between navigations (fetch buffers are "sticky" and survive)

## browser host (the only JavaScript)

~250 lines, no framework: node registry, command-batch processor, event
registration, history API + link interception, fetch plumbing with bounded
response slots, hydration handshake. Loaded as `/assets/host.js`.
