#include "guestbook.h"

#include <pthread.h>
#include <string.h>
#include <time.h>

#define GB_CAP 32
#define GB_NAME_MAX 40
#define GB_MSG_MAX 280

typedef struct {
  char name[GB_NAME_MAX];
  char message[GB_MSG_MAX];
  char when[20];
} gb_slot;

static gb_slot g_slots[GB_CAP];
static size_t g_head;   /* next write position */
static size_t g_count;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* snapshot handed to the rendering handler: it reads these after the lock
 * is released, so the data must be copies, not views into the ring */
static guestbook_entry g_view[GB_CAP];
static char g_view_name[GB_CAP][GB_NAME_MAX];
static char g_view_msg[GB_CAP][GB_MSG_MAX];
static char g_view_when[GB_CAP][20];

static void copy_truncated(char *dst, size_t cap, const char *src) {
  size_t i = 0;
  for (; src[i] && i + 1 < cap; i++) dst[i] = src[i] == '\n' ? ' ' : src[i];
  dst[i] = 0;
}

void guestbook_add(const char *name, const char *message) {
  if (!name || !message) return;
  pthread_mutex_lock(&g_lock);
  gb_slot *slot = &g_slots[g_head];
  copy_truncated(slot->name, sizeof(slot->name), name);
  copy_truncated(slot->message, sizeof(slot->message), message);
  time_t now = time(NULL);
  struct tm tm;
  localtime_r(&now, &tm);
  strftime(slot->when, sizeof(slot->when), "%Y-%m-%d %H:%M", &tm);
  g_head = (g_head + 1) % GB_CAP;
  if (g_count < GB_CAP) g_count++;
  pthread_mutex_unlock(&g_lock);
}

const guestbook_entry *guestbook_entries(size_t *n) {
  pthread_mutex_lock(&g_lock);
  for (size_t i = 0; i < g_count; i++) {
    size_t idx = (g_head + GB_CAP - 1 - i) % GB_CAP;
    memcpy(g_view_name[i], g_slots[idx].name, GB_NAME_MAX);
    memcpy(g_view_msg[i], g_slots[idx].message, GB_MSG_MAX);
    memcpy(g_view_when[i], g_slots[idx].when, 20);
    g_view[i].name = g_view_name[i];
    g_view[i].message = g_view_msg[i];
    g_view[i].when = g_view_when[i];
  }
  *n = g_count;
  pthread_mutex_unlock(&g_lock);
  return g_view;
}
