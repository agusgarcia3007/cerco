/* freestanding wasm32: compiler-provided headers (stddef/stdint/stdarg) plus
 * declarations for the routines the client runtime itself implements. */
#ifndef CERCO_WASM_LIBC_H
#define CERCO_WASM_LIBC_H

#include <stddef.h>

void *memcpy(void *, const void *, size_t);
void *memmove(void *, const void *, size_t);
void *memset(void *, int, size_t);
size_t strlen(const char *);
int strcmp(const char *, const char *);
int memcmp(const void *, const void *, size_t);
char *strstr(const char *, const char *);
char *strcpy(char *, const char *);

#endif
