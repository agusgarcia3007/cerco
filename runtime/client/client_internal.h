/* cerco client runtime internals (not part of the public API) */
#ifndef CERCO_CLIENT_INTERNAL_H
#define CERCO_CLIENT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

/* wire value types (must match runtime/shared/wire.h) */
#define CERCO_WT_I32_LOCAL 0
#define CERCO_WT_I64_LOCAL 1
#define CERCO_WT_F64_LOCAL 2
#define CERCO_WT_BOOL_LOCAL 3
#define CERCO_WT_STR_LOCAL 4
#define CERCO_WT_BYTES_LOCAL 5

/* host imports (implemented in runtime/browser/host.js) */
__attribute__((import_module("cerco"), import_name("dom_flush")))
extern void host_dom_flush(int32_t ptr, int32_t len);
__attribute__((import_module("cerco"), import_name("query")))
extern int32_t host_query(int32_t scope, int32_t sel_ptr, int32_t sel_len);
__attribute__((import_module("cerco"), import_name("value")))
extern int32_t host_value(int32_t node, int32_t out_ptr, int32_t cap);
__attribute__((import_module("cerco"), import_name("fetch")))
extern void host_fetch(int32_t id, int32_t method_ptr, int32_t method_len,
                       int32_t url_ptr, int32_t url_len, int32_t body_ptr,
                       int32_t body_len, int32_t resp_ptr, int32_t resp_cap);
__attribute__((import_module("cerco"), import_name("nav_push")))
extern void host_nav_push(int32_t url_ptr, int32_t url_len);
__attribute__((import_module("cerco"), import_name("set_title")))
extern void host_set_title(int32_t ptr, int32_t len);
__attribute__((import_module("cerco"), import_name("log")))
extern void host_log(int32_t ptr, int32_t len);
__attribute__((import_module("cerco"), import_name("location")))
extern int32_t host_location(int32_t ptr, int32_t cap);
__attribute__((import_module("cerco"), import_name("root_id")))
extern int32_t host_root_id(int32_t index);
__attribute__((import_module("cerco"), import_name("hydrate_roots")))
extern int32_t host_hydrate_roots(void);
__attribute__((import_module("cerco"), import_name("nav_reload")))
extern void host_nav_reload(void);

/* runtime internals (implemented across env.c / dom.c / hydrate.c) */
void *cerco_alloc_sticky(size_t n);
uint8_t *cerco_scratch_base(void);
void cerco_scratch_advance(size_t n);
void cerco_scratch_reset(void);
void cerco_heap_rewind(void);
void cerco_heap_init(void);
void cerco_dom_flush(void);
void cerco_events_reset(void);
void cerco_roots_reset(void);
int32_t cerco_i32_to_str(char *buf, int32_t v);
void cerco_debug_log(const char *s);

#endif
