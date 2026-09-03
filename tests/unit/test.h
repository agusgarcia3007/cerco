/* cerco unit test harness */
#ifndef CERCO_TEST_H
#define CERCO_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_tests_run;
extern int g_tests_failed;

#define TEST(name)                                                        \
  static void test_##name(void);                                          \
  static void test_##name(void)

#define CHECK(cond)                                                       \
  do {                                                                    \
    g_tests_run++;                                                        \
    if (!(cond)) {                                                        \
      g_tests_failed++;                                                   \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);     \
    }                                                                     \
  } while (0)

#define CHECK_STR_EQ(a, b)                                                \
  do {                                                                    \
    g_tests_run++;                                                        \
    const char *_a = (a), *_b = (b);                                      \
    if (!_a || !_b || strcmp(_a, _b) != 0) {                              \
      g_tests_failed++;                                                   \
      fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__,         \
              __LINE__, _a ? _a : "(null)", _b ? _b : "(null)");          \
    }                                                                     \
  } while (0)

#define CHECK_INT_EQ(a, b)                                                \
  do {                                                                    \
    g_tests_run++;                                                        \
    long long _a = (long long)(a), _b = (long long)(b);                   \
    if (_a != _b) {                                                       \
      g_tests_failed++;                                                   \
      fprintf(stderr, "FAIL %s:%d: %lld != %lld\n", __FILE__, __LINE__,   \
              _a, _b);                                                    \
    }                                                                     \
  } while (0)

#define TEST_MAIN(name)                                                   \
  int main(void) {                                                        \
    name();                                                               \
    printf("%d checks, %d failures\n", g_tests_run, g_tests_failed);      \
    return g_tests_failed ? 1 : 0;                                        \
  }

#endif
