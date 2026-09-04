# browser runtime (host ABI)

The host (`runtime/browser/host.js`) is the only JavaScript in the framework:
~250 lines, no dependencies, no framework.

## imports (wasm -> host, module "cerco")

| import | purpose |
|---|---|
| dom_flush(ptr, len) | process one DOM command batch |
| query(scope, sel, len) | querySelector; returns a node id |
| value(node, out, cap) | read input value into wasm |
| fetch(id, method, url, body) | async fetch; the client hands back a buffer |
| nav_push(url) | history.pushState |
| set_title(ptr, len) | document.title |
| log(ptr, len) | console.log |
| location(ptr, cap) | current path |
| root_id(i) | node id of hydration root i |
| hydrate_roots() | scan [data-cerco], assign ids, call mounts |
| nav_reload() | location.reload fallback |
| swap_page(ptr, len) | replace the nodes between the `cerco:page` markers |

## exports (host -> wasm)

cerco_boot, cerco_hydrate_root(i), cerco_hydrate_all, cerco_event(node, slot),
cerco_fetch_reserve(id, len), cerco_fetch_done(id, status, len),
cerco_navigate(path_len), cerco_scratch(), cerco_scratch_size().

## DOM command buffer

Flat byte buffer, variable-length commands, little-endian:

```
1 CREATE {id, tag, parent}    7 SET_CLASS {node, cls}
2 SET_TEXT {node, text}       8 ADD_CLASS {node, cls}
3 SET_ATTR {node, k, v}       9 REMOVE_CLASS {node, cls}
4 REMOVE_ATTR {node, k}       10 ADD_EVENT {node, type, slot}
5 APPEND_CHILD {p, c}         11 SET_VALUE {node, v}
6 REMOVE_NODE {node}          12 SET_INNER_HTML {node, html}
```

Strings are inline `[u32 len][bytes]`. Batches flush once per state change —
one wasm->JS crossing per interaction, not per DOM op.

SET_INNER_HTML drops only the event handlers registered inside the subtree it
replaces; handlers elsewhere on the page keep working.

## fetch

The host does not own the response buffer. When a response arrives it calls
`cerco_fetch_reserve(id, len)`, the client allocates a buffer of exactly that
size (plus a NUL, so text bodies are valid C strings) and returns its address,
and only then does the host copy the bytes in and call `cerco_fetch_done`.

Two consequences worth knowing:

- A body is delivered whole or not at all. Over `CERCO_FETCH_MAX` (512 KiB by
  default, `-DCERCO_FETCH_MAX=` to change) the reserve returns 0 and the
  callback sees `CERCO_HTTP_TOO_LARGE` (-1) instead of a truncated body.
- The host must re-read `memory.buffer` *after* the reserve call: allocating
  can grow the wasm heap, which detaches every ArrayBuffer taken before it.

## events

`cerco_on(node, "click", fn, user)` registers the handler in a wasm table
(bounded 512 slots) and emits ADD_EVENT; the host adds one real listener per
(node, type) pair and dispatches `cerco_event(node, slot)`.

## hydration

1. host scans `[data-cerco]` roots (document order), assigns node ids
2. per root: host writes `[u32 name_len][name][u32 props_len][props]` at the
   wasm scratch base and calls cerco_hydrate_root(i)
3. the generated registry finds the component's mount fn; mount binds signals
   to existing DOM (cerco_query finds `[data-cerco-b=...]` markers)
4. SSR HTML is never re-rendered

## navigation

Link clicks on same-origin `/` paths and popstate call `cerco_navigate`:
fetch page -> extract `<!--cerco:page-->...<!--cerco:/page-->` -> `swap_page`
-> update title -> re-hydrate. The heap rewinds between pages so navigation
cannot grow memory.

`swap_page` replaces only the nodes *between* the markers. Everything the
layout renders outside them — header, nav, footer — is never touched, so it
does not flicker, lose scroll position or drop its event handlers on
navigation. If the markers are missing the client falls back to a full
document load.
