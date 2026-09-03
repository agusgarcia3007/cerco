/* todo example: server functions mutate state, client re-renders via signals */
#include <cerco.h>

#include "state.h"

todo_state g_todos;

CERCO_ROUTE {
  cerco_tag(r, "html", NULL) {
    cerco_tag(r, "head", NULL) {
      cerco_tag(r, "title", NULL) { cerco_raw(r, "todo — cerco"); }
      cerco_raw(r, "<script src=\"/assets/host.js\" defer></script>");
    }
    cerco_tag(r, "body", CERCO_CLASS("font-sans max-w-md mx-auto p-8")) {
      cerco_tag(r, "h1", CERCO_CLASS("text-2xl font-bold mb-4")) { cerco_raw(r, "todo"); }
      cerco_tag(r, "div", CERCO_COMPONENT(r, "todolist", "{}")) {
        cerco_tag(r, "ul", CERCO_ATTRS({"data-cerco-b", "list"}, {"class", "list-disc ml-6"})) {
          for (int i = 0; i < g_todos.count; i++) {
            cerco_tag(r, "li", NULL) {
              cerco_textf(r, "%s%s", g_todos.text[i], g_todos.done[i] ? " (done)" : "");
            }
          }
        }
        cerco_tag(r, "p", CERCO_CLASS("mt-4 text-slate-500 text-sm")) {
          cerco_raw(r, "add items from the browser console: __cerco_todo(\"buy milk\")");
        }
      }
    }
  }
}
