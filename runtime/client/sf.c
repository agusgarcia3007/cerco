/* Server-function client plumbing: arg encoding, HTTP round trip, decode.
 * Generated stubs (from the app's SF declarations) are thin wrappers. */
#include "cerco_client.h"
#include "client_internal.h"
#include "wasm_libc.h"

/* ---- wire encoding (same layout as runtime/shared/wire.h, LE) ---- */

static void put_u32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static uint32_t get_u32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

#define SF_BODY_MAX 32768

static uint8_t *sf_body;
static size_t sf_body_len;
static uint32_t sf_id;
static uint32_t sf_nargs;

void cerco_sf_begin(uint32_t id, uint32_t nargs) {
  if (!sf_body) sf_body = cerco_alloc_sticky(SF_BODY_MAX);
  sf_body_len = 0;
  sf_id = id;
  sf_nargs = nargs;
}

static void sf_reserve(size_t n) {
  /* space is bounded; overflows simply drop (args checked by caller count) */
  if (sf_body_len + n > SF_BODY_MAX) return;
}

void cerco_sf_arg_i32(int32_t v) {
  sf_reserve(5);
  sf_body[sf_body_len++] = CERCO_WT_I32_LOCAL;
  put_u32(sf_body + sf_body_len, (uint32_t)v);
  sf_body_len += 4;
}

void cerco_sf_arg_bool(int32_t v) {
  sf_reserve(5);
  sf_body[sf_body_len++] = CERCO_WT_BOOL_LOCAL;
  put_u32(sf_body + sf_body_len, v ? 1u : 0u);
  sf_body_len += 4;
}

void cerco_sf_arg_i64(int64_t v) {
  sf_reserve(9);
  sf_body[sf_body_len++] = CERCO_WT_I64_LOCAL;
  uint64_t u = (uint64_t)v;
  for (int i = 0; i < 8; i++) sf_body[sf_body_len + i] = (uint8_t)(u >> (i * 8));
  sf_body_len += 8;
}

void cerco_sf_arg_f64(double v) {
  sf_reserve(9);
  sf_body[sf_body_len++] = CERCO_WT_F64_LOCAL;
  uint64_t u;
  memcpy(&u, &v, 8);
  for (int i = 0; i < 8; i++) sf_body[sf_body_len + i] = (uint8_t)(u >> (i * 8));
  sf_body_len += 8;
}

void cerco_sf_arg_str(const char *s) {
  uint32_t n = s ? (uint32_t)strlen(s) : 0;
  if (sf_body_len + 5 + n > SF_BODY_MAX) { sf_body_len = SF_BODY_MAX + 1; return; }
  sf_body[sf_body_len++] = CERCO_WT_STR_LOCAL;
  put_u32(sf_body + sf_body_len, n);
  sf_body_len += 4;
  if (n) {
    memcpy(sf_body + sf_body_len, s, n);
    sf_body_len += n;
  }
}

void cerco_sf_arg_bytes(const uint8_t *d, int32_t len) {
  uint32_t n = len > 0 ? (uint32_t)len : 0;
  if (sf_body_len + 5 + n > SF_BODY_MAX) { sf_body_len = SF_BODY_MAX + 1; return; }
  sf_body[sf_body_len++] = CERCO_WT_BYTES_LOCAL;
  put_u32(sf_body + sf_body_len, n);
  sf_body_len += 4;
  if (n) {
    memcpy(sf_body + sf_body_len, d, n);
    sf_body_len += n;
  }
}

/* ---- response decode ---- */

static const char *sf_url_for(uint32_t id) {
  char *url = cerco_scratch_strdup("/__cerco/sf/");
  if (!url) return 0;
  /* append decimal id */
  char tmp[12];
  int i = 0;
  uint32_t u = id;
  if (!u) tmp[i++] = '0';
  while (u) { tmp[i++] = (char)('0' + u % 10); u /= 10; }
  size_t base = strlen("/__cerco/sf/");
  char *out = url;
  size_t o = base;
  while (i) out[o++] = tmp[--i];
  out[o] = 0;
  return out;
}

typedef struct {
  void *cb;
  void *user;
  uint8_t want_type;
} sf_pending;

#define SF_PENDING_MAX 32
static sf_pending g_sf_pending[SF_PENDING_MAX];

static void sf_done_common(int status, const uint8_t *data, int32_t len, void *user);

void cerco_sf_submit_raw(void *cb, void *user, uint8_t want_type) {
  int slot = -1;
  for (int i = 0; i < SF_PENDING_MAX; i++) {
    if (!g_sf_pending[i].cb) { slot = i; break; }
  }
  if (slot < 0 || sf_body_len + 4 > SF_BODY_MAX) return; /* bounded reject */
  /* frame header: [u32 arg count][args...] */
  uint8_t tmp[4];
  memmove(sf_body + 4, sf_body, sf_body_len);
  put_u32(tmp, sf_nargs);
  memcpy(sf_body, tmp, 4);
  sf_body_len += 4;

  g_sf_pending[slot].cb = cb;
  g_sf_pending[slot].user = user;
  g_sf_pending[slot].want_type = want_type;

  const char *url = sf_url_for(sf_id);
  cerco_http_post(url, sf_body, (int32_t)sf_body_len, sf_done_common,
                  (void *)(intptr_t)slot);
}

static void sf_done_common(int status, const uint8_t *data, int32_t len, void *user) {
  int slot = (int)(intptr_t)user;
  sf_pending p = g_sf_pending[slot];
  g_sf_pending[slot].cb = 0;
  if (!p.cb) return;
  if (status != 200 || !data || len < 9) {
    ((cerco_sf_cb_i32)p.cb)(0, "network error", p.user);
    return;
  }
  uint8_t st = data[0];
  uint32_t err_len = get_u32(data + 1);
  if (err_len > (uint32_t)len) { ((cerco_sf_cb_i32)p.cb)(0, "bad response", p.user); return; }
  if (st != 0) {
    /* error message in scratch-stable fetch buffer; callbacks must copy */
    char *msg = cerco_scratch_alloc(err_len + 1);
    if (msg) {
      memcpy(msg, data + 5, err_len);
      msg[err_len] = 0;
      ((cerco_sf_cb_i32)p.cb)(0, msg, p.user);
    } else {
      ((cerco_sf_cb_i32)p.cb)(0, "error", p.user);
    }
    return;
  }
  const uint8_t *vals = data + 5 + err_len;
  if (vals + 4 > data + (size_t)len) { ((cerco_sf_cb_i32)p.cb)(0, "bad response", p.user); return; }
  uint32_t vcount = get_u32(vals);
  const uint8_t *v = vals + 4;
  if (vcount < 1 || v >= data + (size_t)len) { ((cerco_sf_cb_i32)p.cb)(0, "bad response", p.user); return; }
  uint8_t vtype = *v++;
  if (vtype != p.want_type) {
    /* bool widening */
    if (!(p.want_type == CERCO_WT_BOOL_LOCAL && vtype == CERCO_WT_I32_LOCAL) &&
        !(p.want_type == CERCO_WT_I32_LOCAL && vtype == CERCO_WT_BOOL_LOCAL)) {
      ((cerco_sf_cb_i32)p.cb)(0, "type mismatch", p.user);
      return;
    }
  }
  /* decode single value; callbacks receive pointers into the fetch buffer */
  switch (vtype) {
    case CERCO_WT_I32_LOCAL:
    case CERCO_WT_BOOL_LOCAL:
      ((cerco_sf_cb_i32)p.cb)((int32_t)get_u32(v), 0, p.user);
      break;
    case CERCO_WT_I64_LOCAL: {
      uint64_t u = 0;
      for (int i = 0; i < 8; i++) u |= ((uint64_t)v[i]) << (i * 8);
      ((cerco_sf_cb_i64)p.cb)((int64_t)u, 0, p.user);
      break;
    }
    case CERCO_WT_F64_LOCAL: {
      uint64_t u = 0;
      for (int i = 0; i < 8; i++) u |= ((uint64_t)v[i]) << (i * 8);
      double d;
      memcpy(&d, &u, 8);
      ((cerco_sf_cb_f64)p.cb)(d, 0, p.user);
      break;
    }
    case CERCO_WT_STR_LOCAL: {
      uint32_t n = get_u32(v);
      ((cerco_sf_cb_str)p.cb)((const char *)v + 4, 0, p.user);
      (void)n;
      break;
    }
    case CERCO_WT_BYTES_LOCAL: {
      uint32_t n = get_u32(v);
      ((cerco_sf_cb_bytes)p.cb)(v + 4, (int32_t)n, 0, p.user);
      break;
    }
    default:
      ((cerco_sf_cb_i32)p.cb)(0, "type mismatch", p.user);
      break;
  }
}
