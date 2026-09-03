#include "test.h"

int g_tests_run = 0;
int g_tests_failed = 0;

void main_2(void);
void main_router(void);

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  main_2();
  main_router();
  printf("%d checks, %d failures\n", g_tests_run, g_tests_failed);
  return g_tests_failed ? 1 : 0;
}
