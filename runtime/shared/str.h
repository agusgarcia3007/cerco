/* cerco — string utilities: HTML escaping, URL percent-decoding, MIME lookup. */
#ifndef CERCO_STR_H
#define CERCO_STR_H

#include <stddef.h>
#include <stdint.h>

/* --- HTML escaping ------------------------------------------------------- */

/* escape into caller buffer; returns bytes written (excl NUL). Always NUL-
 * terminates when cap > 0. Returns needed length (excl NUL) if buf is NULL. */
size_t cerco_html_escape(const char *src, size_t len, char *buf, size_t cap);

/* --- URL ----------------------------------------------------------------- */

/* percent-decode in place-ish: writes to dst, at most dstcap bytes, always
 * NUL-terminated when dstcap > 0. '+' becomes ' ' when plus_is_space.
 * Returns decoded length (excl NUL) or (size_t)-1 on invalid escape. */
size_t cerco_url_decode(const char *src, size_t len, char *dst, size_t dstcap,
                        int plus_is_space);

/* query string "a=1&b=2" walker: yields next key/value (both decoded into
 * dst buffers). Returns 1 when a pair was produced, 0 when done, -1 on
 * malformed input. *pos must be initialized to 0. */
int cerco_query_next(const char *q, size_t *pos, char *kbuf, size_t kcap,
                     char *vbuf, size_t vcap);

/* --- MIME ---------------------------------------------------------------- */

const char *cerco_mime_from_ext(const char *filename); /* default application/octet-stream */

/* hex encode / decode helpers (asset embedding, etags) */
void cerco_hex_encode(const uint8_t *src, size_t len, char *dst); /* dst needs 2*len+1 */
int cerco_hex_decode(const char *src, size_t len, uint8_t *dst);   /* returns 0/1 */

/* case-insensitive compare (locale independent) */
int cerco_strcaseeq(const char *a, const char *b);
int cerco_strncaseeq(const char *a, const char *b, size_t n);

#endif
