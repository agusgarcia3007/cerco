#include <cerco.h>
#include <stdio.h>

#include "state.h"

int32_t sf_todo_add(cerco_sf_ctx *ctx, const char *text) {
  if (g_todos.count >= TODO_MAX) {
    cerco_sf_fail(ctx, "todo list full");
    return -1;
  }
  if (!text || !text[0]) {
    cerco_sf_fail(ctx, "empty todo");
    return -1;
  }
  snprintf(g_todos.text[g_todos.count], sizeof(g_todos.text[0]), "%s", text);
  g_todos.done[g_todos.count] = 0;
  return g_todos.count++;
}

int32_t sf_todo_toggle(cerco_sf_ctx *ctx, int32_t index) {
  if (index < 0 || index >= g_todos.count) {
    cerco_sf_fail(ctx, "bad index");
    return -1;
  }
  g_todos.done[index] = !g_todos.done[index];
  return g_todos.done[index];
}
