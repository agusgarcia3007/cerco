/* Forward-scanning JSON readers for client components.
 *
 * The wasm client has no libc and no JSON library — and does not need one to
 * consume a REST API. These readers walk the response looking for a key and
 * copy out its value, which is all a page like this actually does. They
 * assume well-formed JSON with unescaped string values (what the PokeAPI
 * returns); a hostile or exotic feed would want a real parser.
 */
#ifndef CERCO_JSON_SCAN_H
#define CERCO_JSON_SCAN_H

#include <cerco_client.h>

/* copy the quoted string at p (p must point at the opening quote) into out.
 * Returns the position just after the closing quote, or 0 on a malformed or
 * over-long value. */
static inline const char *cj_string(const char *p, char *out, int cap) {
  if (!p || *p != '"') return 0;
  p++;
  int o = 0;
  while (*p && *p != '"') {
    if (o + 1 >= cap) return 0;
    out[o++] = *p++;
  }
  if (*p != '"') return 0;
  out[o] = 0;
  return p + 1;
}

/* position just after the colon of the next "key": in from, or 0 */
static inline const char *cj_seek(const char *from, const char *key) {
  if (!from) return 0;
  char pat[48];
  if (cerco_format(pat, (int32_t)sizeof(pat), "\"%s\":", key) <= 0) return 0;
  const char *p = strstr(from, pat);
  if (!p) return 0;
  p += strlen(pat);
  while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++;
  return p;
}

/* find "key" and copy its string value into out. Returns the scan position
 * after the value so a loop can keep walking, or 0 when the key is absent. */
static inline const char *cj_find_str(const char *from, const char *key,
                                      char *out, int cap) {
  return cj_string(cj_seek(from, key), out, cap);
}

/* find "key" and read its integer value; dflt when absent or not a number */
static inline int cj_find_int(const char *from, const char *key, int dflt) {
  const char *p = cj_seek(from, key);
  if (!p) return dflt;
  int sign = 1;
  if (*p == '-') { sign = -1; p++; }
  if (*p < '0' || *p > '9') return dflt;
  int v = 0;
  while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
  return v * sign;
}

/* trailing integer of a ".../pokemon/25/" style URL, or 0 */
static inline int cj_url_tail_int(const char *url) {
  int end = (int)strlen(url);
  while (end > 0 && (url[end - 1] == '/' || url[end - 1] == ' ')) end--;
  int start = end;
  while (start > 0 && url[start - 1] >= '0' && url[start - 1] <= '9') start--;
  if (start == end) return 0;
  int v = 0;
  for (int i = start; i < end; i++) v = v * 10 + (url[i] - '0');
  return v;
}

#endif
