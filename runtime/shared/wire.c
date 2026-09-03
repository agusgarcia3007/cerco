#include "wire.h"
#include <string.h>

void cerco_wreader_init(cerco_wreader *r, const void *data, size_t len) {
  r->p = (const uint8_t *)data;
  r->len = len;
  r->pos = 0;
  r->err = 0;
}

static const uint8_t *r_take(cerco_wreader *r, size_t n) {
  if (r->err || n > r->len - r->pos) { r->err = 1; return NULL; }
  const uint8_t *p = r->p + r->pos;
  r->pos += n;
  return p;
}

uint8_t cerco_r_u8(cerco_wreader *r) {
  const uint8_t *p = r_take(r, 1);
  return p ? p[0] : 0;
}

uint32_t cerco_r_u32(cerco_wreader *r) {
  const uint8_t *p = r_take(r, 4);
  if (!p) return 0;
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

uint64_t cerco_r_u64(cerco_wreader *r) {
  const uint8_t *p = r_take(r, 8);
  if (!p) return 0;
  return (uint64_t)p[0] | ((uint64_t)p[1] << 8) | ((uint64_t)p[2] << 16) |
         ((uint64_t)p[3] << 24) | ((uint64_t)p[4] << 32) | ((uint64_t)p[5] << 40) |
         ((uint64_t)p[6] << 48) | ((uint64_t)p[7] << 56);
}

int cerco_r_val(cerco_wreader *r, cerco_wval *out) {
  uint8_t t = cerco_r_u8(r);
  if (r->err) return 0;
  out->type = t;
  out->bytes.data = NULL;
  out->bytes.len = 0;
  out->as.i64 = 0;
  switch (t) {
    case CERCO_WT_I32:
    case CERCO_WT_BOOL: {
      /* bool arrives as 0/1 in the i32 slot; as.b reads the low byte */
      out->as.i32 = (int32_t)cerco_r_u32(r);
      return !r->err;
    }
    case CERCO_WT_I64: {
      out->as.i64 = (int64_t)cerco_r_u64(r);
      return !r->err;
    }
    case CERCO_WT_F64: {
      uint64_t bits = cerco_r_u64(r);
      memcpy(&out->as.f64, &bits, 8);
      return !r->err;
    }
    case CERCO_WT_STR:
    case CERCO_WT_BYTES: {
      uint32_t len = cerco_r_u32(r);
      const uint8_t *p = r_take(r, len);
      if (!p) return 0;
      out->bytes.data = p;
      out->bytes.len = len;
      return 1;
    }
    default:
      r->err = 1;
      return 0;
  }
}

void cerco_wwriter_init(cerco_wwriter *w, void *buf, size_t cap) {
  w->buf = (uint8_t *)buf;
  w->cap = cap;
  w->pos = 0;
  w->err = 0;
}

static uint8_t *w_take(cerco_wwriter *w, size_t n) {
  if (w->err || n > w->cap - w->pos) { w->err = 1; return NULL; }
  uint8_t *p = w->buf + w->pos;
  w->pos += n;
  return p;
}

void cerco_w_u8(cerco_wwriter *w, uint8_t v) {
  uint8_t *p = w_take(w, 1);
  if (p) p[0] = v;
}

void cerco_w_u32(cerco_wwriter *w, uint32_t v) {
  uint8_t *p = w_take(w, 4);
  if (p) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
  }
}

static void w_u64_raw(uint8_t *p, uint64_t v) {
  for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (i * 8));
}

void cerco_w_u64(cerco_wwriter *w, uint64_t v) {
  uint8_t *p = w_take(w, 8);
  if (p) w_u64_raw(p, v);
}

void cerco_w_f64(cerco_wwriter *w, double v) {
  uint64_t bits;
  memcpy(&bits, &v, 8);
  cerco_w_u64(w, bits);
}

void cerco_w_val(cerco_wwriter *w, const cerco_wval *v) {
  cerco_w_u8(w, v->type);
  switch (v->type) {
    case CERCO_WT_I32: cerco_w_u32(w, (uint32_t)v->as.i32); break;
    case CERCO_WT_BOOL: cerco_w_u32(w, v->as.b ? 1u : 0u); break;
    case CERCO_WT_I64: cerco_w_u64(w, (uint64_t)v->as.i64); break;
    case CERCO_WT_F64: cerco_w_f64(w, v->as.f64); break;
    case CERCO_WT_STR:
    case CERCO_WT_BYTES:
      cerco_w_u32(w, (uint32_t)v->bytes.len);
      if (v->bytes.len) {
        uint8_t *p = w_take(w, v->bytes.len);
        if (p) memcpy(p, v->bytes.data, v->bytes.len);
      }
      break;
    default: w->err = 1; break;
  }
}

size_t cerco_wval_size(const cerco_wval *v) {
  size_t n = 1;
  switch (v->type) {
    case CERCO_WT_I32: case CERCO_WT_BOOL: return n + 4;
    case CERCO_WT_I64: case CERCO_WT_F64: return n + 8;
    case CERCO_WT_STR: case CERCO_WT_BYTES: return n + 4 + v->bytes.len;
    default: return n;
  }
}
