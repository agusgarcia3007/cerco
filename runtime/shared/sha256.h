/* cerco — SHA-256 (FIPS 180-4). Used for ETags and build hashes. */
#ifndef CERCO_SHA256_H
#define CERCO_SHA256_H

#include <stddef.h>
#include <stdint.h>

typedef struct cerco_sha256_ctx {
  uint32_t state[8];
  uint64_t bits;
  uint8_t block[64];
  size_t used;
} cerco_sha256_ctx;

void cerco_sha256_init(cerco_sha256_ctx *c);
void cerco_sha256_update(cerco_sha256_ctx *c, const void *data, size_t len);
void cerco_sha256_final(cerco_sha256_ctx *c, uint8_t out[32]);
void cerco_sha256(const void *data, size_t len, uint8_t out[32]);
/* lowercase hex (65 bytes incl NUL) */
void cerco_sha256_hex(const void *data, size_t len, char out[65]);

#endif
