/* cerco — server-function wire codec.
 *
 * Explicit binary format; never relies on native struct layout (native and
 * wasm32 have different pointer widths/alignment).
 *
 * Value encoding:  [u8 type][u32 len][payload bytes]
 *   - CERCO_WT_I32 / CERCO_WT_BOOL : len ignored, 4 bytes LE payload
 *   - CERCO_WT_I64 / CERCO_WT_F64  : len ignored, 8 bytes LE payload
 *   - CERCO_WT_STR                 : payload = UTF-8 bytes (no NUL)
 *   - CERCO_WT_BYTES               : payload = raw bytes
 *
 * Request payload : [u32 arg_count] then arg values in order.
 * Response payload: [u8 status][u32 err_len][err bytes, if status != 0]
 *                   [u32 value_count] then values (status 0 only).
 */
#ifndef CERCO_WIRE_H
#define CERCO_WIRE_H

#include <stddef.h>
#include <stdint.h>

enum {
  CERCO_WT_I32 = 0,
  CERCO_WT_I64 = 1,
  CERCO_WT_F64 = 2,
  CERCO_WT_BOOL = 3,
  CERCO_WT_STR = 4,
  CERCO_WT_BYTES = 5,
};

typedef struct {
  const uint8_t *data;
  size_t len;
} cerco_wbytes;

typedef struct {
  uint8_t type;
  union {
    int32_t i32;
    int64_t i64;
    double f64;
    uint8_t b;
  } as;
  cerco_wbytes bytes; /* str/bytes */
} cerco_wval;

/* --- reader -------------------------------------------------------------- */

typedef struct {
  const uint8_t *p;
  size_t len;
  size_t pos;
  int err; /* 1 = malformed / truncated */
} cerco_wreader;

void cerco_wreader_init(cerco_wreader *r, const void *data, size_t len);
uint8_t cerco_r_u8(cerco_wreader *r);
uint32_t cerco_r_u32(cerco_wreader *r);
uint64_t cerco_r_u64(cerco_wreader *r);
int cerco_r_val(cerco_wreader *r, cerco_wval *out); /* 1 ok, 0 error/EOF */

/* --- writer -------------------------------------------------------------- */

typedef struct {
  uint8_t *buf;   /* caller-provided storage */
  size_t cap;
  size_t pos;
  int err;        /* 1 = out of space */
} cerco_wwriter;

void cerco_wwriter_init(cerco_wwriter *w, void *buf, size_t cap);
void cerco_w_u8(cerco_wwriter *w, uint8_t v);
void cerco_w_u32(cerco_wwriter *w, uint32_t v);
void cerco_w_u64(cerco_wwriter *w, uint64_t v);
void cerco_w_f64(cerco_wwriter *w, double v);
void cerco_w_val(cerco_wwriter *w, const cerco_wval *v);
/* size needed to encode v */
size_t cerco_wval_size(const cerco_wval *v);

#endif
