#include "internal.h"
#include <stdlib.h>
#include <string.h>

int cerco_parse_i64(const char *s, long long *out) {
  if (!s || !*s) return 0;
  long long v = 0;
  int neg = 0;
  if (*s == '-') { neg = 1; s++; }
  if (!*s) return 0;
  while (*s) {
    if (*s < '0' || *s > '9') return 0;
    if (v > (9223372036854775807LL - (*s - '0')) / 10) return 0;
    v = v * 10 + (*s - '0');
    s++;
  }
  *out = neg ? -v : v;
  return 1;
}

static const char *env_or(const char *k, const char *dflt) {
  const char *v = getenv(k);
  return (v && *v) ? v : dflt;
}

void cerco_config_from_env(cerco_config *cfg) {
  memset(cfg, 0, sizeof(*cfg));
  cfg->host = env_or("HOST", "0.0.0.0");
  long long port = 3000;
  if (!cerco_parse_i64(env_or("PORT", "3000"), &port) || port < 1 || port > 65535) port = 3000;
  cfg->port = (int)port;

  long long workers = cerco_cpu_count();
  if (workers > 4) workers = 4;
  if (workers < 1) workers = 1;
  long long env_workers;
  if (cerco_parse_i64(env_or("CERCO_WORKERS", ""), &env_workers)) {
    if (env_workers < 1) env_workers = 1;
    if (env_workers > 64) env_workers = 64;
    workers = env_workers;
  }
  cfg->workers = (int)workers;

  long long q = 1024;
  if (cerco_parse_i64(env_or("CERCO_WORK_QUEUE", ""), &q) && q >= 4) {
    if (q > 65536) q = 65536;
  }
  cfg->work_queue = (size_t)q;

  long long conns = 10000;
  if (cerco_parse_i64(env_or("CERCO_MAX_CONNECTIONS", ""), &conns)) {
    if (conns < 8) conns = 8;
    if (conns > 1000000) conns = 1000000;
  }
  cfg->max_connections = conns;

  long long body = 2 * 1024 * 1024;
  if (cerco_parse_i64(env_or("CERCO_MAX_BODY", ""), &body)) {
    if (body < 0) body = 0;
    if (body > 256ull * 1024 * 1024) body = 256ull * 1024 * 1024;
  }
  cfg->max_body = (size_t)body;

  cfg->log_level = CERCO_LOG_INFO;
  const char *lvl = env_or("CERCO_LOG_LEVEL", "info");
  if (cerco_strcaseeq(lvl, "trace")) cfg->log_level = CERCO_LOG_TRACE;
  else if (cerco_strcaseeq(lvl, "debug")) cfg->log_level = CERCO_LOG_DEBUG;
  else if (cerco_strcaseeq(lvl, "info")) cfg->log_level = CERCO_LOG_INFO;
  else if (cerco_strcaseeq(lvl, "warn") || cerco_strcaseeq(lvl, "warning")) cfg->log_level = CERCO_LOG_WARN;
  else if (cerco_strcaseeq(lvl, "error")) cfg->log_level = CERCO_LOG_ERROR;
  else if (cerco_strcaseeq(lvl, "off") || cerco_strcaseeq(lvl, "none")) cfg->log_level = CERCO_LOG_ERROR + 1;

  cfg->trust_proxy = cerco_strcaseeq(env_or("CERCO_TRUST_PROXY", "0"), "1");
  cfg->dev_mode = cerco_strcaseeq(env_or("CERCO_DEV", "0"), "1");
  cfg->stats_enabled = cerco_strcaseeq(env_or("CERCO_STATS", "0"), "1");
  cfg->dist_dir = env_or("CERCO_DIST_DIR", "dist");

  long long t;
  t = 10000; if (cerco_parse_i64(env_or("CERCO_SHUTDOWN_TIMEOUT_MS", ""), &t) && t >= 100 && t <= 120000) {}
  cfg->shutdown_timeout_ms = (uint64_t)t;
  t = 15000; if (cerco_parse_i64(env_or("CERCO_HEADER_TIMEOUT_MS", ""), &t) && t >= 1000 && t <= 120000) {}
  cfg->header_timeout_ms = (uint64_t)t;
  t = 30000; if (cerco_parse_i64(env_or("CERCO_BODY_TIMEOUT_MS", ""), &t) && t >= 1000 && t <= 300000) {}
  cfg->body_timeout_ms = (uint64_t)t;
  t = 75000; if (cerco_parse_i64(env_or("CERCO_KEEPALIVE_TIMEOUT_MS", ""), &t) && t >= 1000 && t <= 600000) {}
  cfg->keep_alive_timeout_ms = (uint64_t)t;
  t = 60000; if (cerco_parse_i64(env_or("CERCO_REQUEST_TIMEOUT_MS", ""), &t) && t >= 1000 && t <= 600000) {}
  cfg->request_timeout_ms = (uint64_t)t;

  cfg->request_arena_first = 16 * 1024;
  cfg->request_arena_cap = 2 * 1024 * 1024;
  long long mem;
  if (cerco_parse_i64(env_or("CERCO_REQUEST_MEMORY_LIMIT", ""), &mem) && mem >= 65536) {
    cfg->request_arena_cap = (size_t)mem;
  }
}
