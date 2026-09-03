# server functions

Call a C function on the server from the wasm client without writing HTTP
boilerplate. Declarations live in an X-macro file; the build generates both
sides.

## declaration

```
/* src/server/functions.x */
CERCO_SF(1, add, CERCO_I32, CERCO_I32, CERCO_I32)
CERCO_SF(2, get_user, CERCO_STR, CERCO_I32)
```

Types: CERCO_I32, CERCO_I64, CERCO_F64, CERCO_BOOL, CERCO_STR, CERCO_BYTES.
IDs are explicit and stable — they are the wire endpoint.

## implementation (server side)

```c
int32_t sf_add(cerco_sf_ctx *ctx, int32_t a, int32_t b) {
  if (a < 0) { cerco_sf_fail(ctx, "no negatives"); return 0; }
  return a + b;
}
```

Handlers run in the worker pool like any request. `cerco_sf_fail` returns an
application error (HTTP status stays 200; the payload carries status+message).
Strings/bytes may point into `cerco_sf_arena(ctx)` (request lifetime).

## call (client side, generated header auto-included)

```c
cerco_sf_add(2, 3, on_result, user);

void on_result(int32_t sum, const char *error, void *user) { ... }
```

Callbacks are async (never block wasm): cerco_sf_submit_raw posts via the
host fetch plumbing; the response is decoded and the typed callback invoked.

## wire format

Request: `POST /__cerco/sf/<id>`, Content-Type `application/x-cerco-sf`

```
[u32 arg_count] [value]*
value = [u8 type][u32 len][payload]   (int/bool/f64 ignore len; LE)
```

Response (application/x-cerco-sf):

```
[u8 status]  0 ok | 1 application error
[u32 err_len][err bytes]     (when status != 0)
[u32 value_count][value]     (when status == 0, single value)
```

The codec is explicit — never a struct memcpy — because native and wasm32
layouts differ.

## validation

Only registered functions (generated table) are reachable. Method must match
(POST), content-type must be application/x-cerco-sf, payload must decode
exactly (truncated/malformed -> 400), bodies capped by CERCO_MAX_BODY.
