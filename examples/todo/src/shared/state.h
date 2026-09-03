/* shared app state (declared here so route + server function TUs agree) */
#define TODO_MAX 16

typedef struct {
  char text[128][TODO_MAX];
  int done[TODO_MAX];
  int count;
} todo_state;

extern todo_state g_todos;
