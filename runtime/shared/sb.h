/* cerco — growable byte buffer.
 *
 * Small dynamic buffer for build-time tooling and bounded response staging.
 * NOT used per-request on the hot path (requests use arenas); owns its own
 * malloc'ed storage and must be sb_free()'d.
 */
#ifndef CERCO_SB_H
#define CERCO_SB_H

#include <stddef.h>
#include <stdint.h>

typedef struct cerco_sb {
  char *buf;
  size_t len;
  size_t cap;
  int oom;
} cerco_sb;

void sb_init(cerco_sb *b);
void sb_reserve(cerco_sb *b, size_t extra);
void sb_putn(cerco_sb *b, const void *data, size_t len);
void sb_puts(cerco_sb *b, const char *s);
void sb_putc(cerco_sb *b, char c);
void sb_printf(cerco_sb *b, const char *fmt, ...);
/* append decimal int / unsigned */
void sb_puti(cerco_sb *b, long long v);
void sb_putu(cerco_sb *b, unsigned long long v);
/* take ownership of the buffer (caller must free) */
char *sb_release(cerco_sb *b);
void sb_reset(cerco_sb *b);
void sb_free(cerco_sb *b);

#endif
