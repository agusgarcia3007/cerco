#include "internal.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static int log_level = CERCO_LOG_INFO;

void cerco_log_set_level(int level) { log_level = level; }

static const char *level_names[] = { "TRACE", "DEBUG", "INFO", "WARN", "ERROR" };

void cerco_log(int level, const char *fmt, ...) {
  if (level < log_level) return;
  if (level < 0 || level > CERCO_LOG_ERROR) level = CERCO_LOG_INFO;
  char buf[4096];
  int off = 0;
  struct timespec wall;
  clock_gettime(CLOCK_REALTIME, &wall);
  time_t secs = wall.tv_sec;
  int64_t now = (int64_t)wall.tv_sec * 1000 + wall.tv_nsec / 1000000;
  struct tm tm;
  gmtime_r(&secs, &tm);
  int n = snprintf(buf, sizeof(buf) - 1,
                   "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ %s ",
                   tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                   tm.tm_hour, tm.tm_min, tm.tm_sec, (int)(now % 1000),
                   level_names[level]);
  if (n > 0 && n < (int)sizeof(buf) - 1) off = n;
  va_list ap;
  va_start(ap, fmt);
  n = vsnprintf(buf + off, sizeof(buf) - off - 1, fmt, ap);
  va_end(ap);
  if (n >= 0 && off + n < (int)sizeof(buf) - 1) {
    off += n;
    buf[off++] = '\n';
  } else {
    off = (int)sizeof(buf) - 2;
    buf[off++] = '\n';
  }
  /* single write keeps interleaving sane across threads */
  ssize_t rc = write(2, buf, (size_t)off);
  (void)rc;
}
