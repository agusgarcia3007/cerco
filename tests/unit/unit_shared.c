#include "test.h"
#include "arena.h"
#include "sb.h"
#include "str.h"
#include "wire.h"
#include "sha256.h"

TEST(arena_basic) {
  cerco_arena a;
  cerco_arena_init(&a, 1024, 4096);
  char *p = cerco_arena_alloc(&a, 100);
  CHECK(p != NULL);
  memset(p, 'x', 100);
  char *q = cerco_arena_alloc0(&a, 64);
  CHECK(q != NULL);
  for (int i = 0; i < 64; i++) CHECK(q[i] == 0);
  char *s = cerco_arena_strdup(&a, "hello");
  CHECK_STR_EQ(s, "hello");
  char *f = cerco_arena_sprintf(&a, "%s-%d", "id", 42);
  CHECK_STR_EQ(f, "id-42");
  cerco_arena_reset(&a);
  /* still usable after reset */
  char *p2 = cerco_arena_alloc(&a, 100);
  CHECK(p2 != NULL);
  CHECK(!cerco_arena_failed(&a));
  cerco_arena_destroy(&a);
}

static void test_oom_noop(void) {}

TEST(arena_hard_cap) {
  cerco_arena_set_oom_handler(test_oom_noop);
  cerco_arena a;
  cerco_arena_init(&a, 256, 1024);
  void *p = cerco_arena_alloc(&a, 512);
  CHECK(p != NULL);
  void *big = cerco_arena_alloc(&a, 4096); /* exceeds cap */
  CHECK(big == NULL);
  CHECK(cerco_arena_failed(&a));
  cerco_arena_destroy(&a);
  cerco_arena_set_oom_handler(NULL); /* restore abort */
}

TEST(arena_alignment) {
  cerco_arena a;
  cerco_arena_init(&a, 1024, 0);
  for (int i = 0; i < 32; i++) {
    void *p = cerco_arena_alloc(&a, 3 + i);
    CHECK(((uintptr_t)p & 7) == 0);
  }
  cerco_arena_destroy(&a);
}

TEST(sb_basics) {
  cerco_sb b;
  sb_init(&b);
  sb_puts(&b, "hello");
  sb_putc(&b, ' ');
  sb_puti(&b, -123);
  sb_puts(&b, " ");
  sb_putu(&b, 18446744073709551615ull);
  sb_printf(&b, " %s=%d", "x", 7);
  CHECK_STR_EQ(b.buf, "hello -123 18446744073709551615 x=7");
  sb_free(&b);
}

TEST(html_escape) {
  char buf[128];
  size_t n = cerco_html_escape("<b>&\"'</b>", 10, buf, sizeof(buf));
  CHECK_STR_EQ(buf, "&lt;b&gt;&amp;&quot;&#39;&lt;/b&gt;");
  CHECK_INT_EQ(n, strlen(buf));
  /* exact-size query mode */
  size_t need = cerco_html_escape("<>", 2, NULL, 0);
  CHECK_INT_EQ(need, 8);
  /* overflow is reported, not truncated silently */
  char small[4];
  CHECK_INT_EQ(cerco_html_escape("&&&&", 4, small, sizeof(small)), (size_t)-1);
}

TEST(url_decode) {
  char buf[64];
  CHECK_INT_EQ(cerco_url_decode("a%20b%2Fc", 9, buf, sizeof(buf), 0), 5);
  CHECK_STR_EQ(buf, "a b/c");
  CHECK_INT_EQ(cerco_url_decode("a+b", 3, buf, sizeof(buf), 1), 3);
  CHECK_STR_EQ(buf, "a b");
  /* strict: truncated and invalid escapes rejected */
  CHECK_INT_EQ(cerco_url_decode("%2", 2, buf, sizeof(buf), 0), (size_t)-1);
  CHECK_INT_EQ(cerco_url_decode("%zz", 3, buf, sizeof(buf), 0), (size_t)-1);
  /* plus is literal when not a form field */
  CHECK_INT_EQ(cerco_url_decode("a+b", 3, buf, sizeof(buf), 0), 3);
  CHECK_STR_EQ(buf, "a+b");
}

TEST(query_parse) {
  const char *q = "a=1&b=hello%20world&flag&c=x%26y";
  size_t pos = 0;
  char k[64], v[64];
  int r = cerco_query_next(q, &pos, k, sizeof(k), v, sizeof(v));
  CHECK_INT_EQ(r, 1);
  CHECK_STR_EQ(k, "a"); CHECK_STR_EQ(v, "1");
  r = cerco_query_next(q, &pos, k, sizeof(k), v, sizeof(v));
  CHECK_INT_EQ(r, 1);
  CHECK_STR_EQ(k, "b"); CHECK_STR_EQ(v, "hello world");
  r = cerco_query_next(q, &pos, k, sizeof(k), v, sizeof(v));
  CHECK_INT_EQ(r, 1);
  CHECK_STR_EQ(k, "flag"); CHECK_STR_EQ(v, "");
  r = cerco_query_next(q, &pos, k, sizeof(k), v, sizeof(v));
  CHECK_INT_EQ(r, 1);
  CHECK_STR_EQ(k, "c"); CHECK_STR_EQ(v, "x&y");
  r = cerco_query_next(q, &pos, k, sizeof(k), v, sizeof(v));
  CHECK_INT_EQ(r, 0);
}

TEST(wire_roundtrip) {
  uint8_t buf[256];
  cerco_wwriter w;
  cerco_wwriter_init(&w, buf, sizeof(buf));
  cerco_wval vals[5] = {0};
  vals[0].type = CERCO_WT_I32; vals[0].as.i32 = -42;
  vals[1].type = CERCO_WT_I64; vals[1].as.i64 = -1234567890123LL;
  vals[2].type = CERCO_WT_F64; vals[2].as.f64 = 3.5;
  vals[3].type = CERCO_WT_BOOL; vals[3].as.b = 1;
  vals[4].type = CERCO_WT_STR;
  vals[4].bytes.data = (const uint8_t *)"hi";
  vals[4].bytes.len = 2;
  for (int i = 0; i < 5; i++) cerco_w_val(&w, &vals[i]);
  CHECK(!w.err);

  cerco_wreader r;
  cerco_wreader_init(&r, buf, w.pos);
  cerco_wval out;
  for (int i = 0; i < 5; i++) {
    CHECK(cerco_r_val(&r, &out));
    CHECK_INT_EQ(out.type, vals[i].type);
    switch (vals[i].type) {
      case CERCO_WT_I32: CHECK_INT_EQ(out.as.i32, vals[i].as.i32); break;
      case CERCO_WT_I64: CHECK_INT_EQ(out.as.i64, vals[i].as.i64); break;
      case CERCO_WT_F64: CHECK(out.as.f64 == vals[i].as.f64); break;
      case CERCO_WT_BOOL: CHECK_INT_EQ(out.as.b, 1); break;
      case CERCO_WT_STR:
        CHECK_INT_EQ(out.bytes.len, 2);
        CHECK(memcmp(out.bytes.data, "hi", 2) == 0);
        break;
    }
  }
  /* EOF is a clean error, not garbage */
  CHECK(!cerco_r_val(&r, &out));
  CHECK(r.err);
}

TEST(wire_truncated) {
  uint8_t buf[8] = {CERCO_WT_STR, 200, 0, 0, 0, 'x', 'x'};
  cerco_wreader r;
  cerco_wreader_init(&r, buf, 7);
  cerco_wval out;
  CHECK(!cerco_r_val(&r, &out)); /* declared len exceeds buffer */
  CHECK(r.err);
}

TEST(wire_fuzz_random) {
  /* malformed inputs must never crash: reader errors are clean */
  unsigned seed = 12345;
  uint8_t buf[64];
  cerco_wreader r;
  cerco_wval out;
  for (int iter = 0; iter < 5000; iter++) {
    size_t len = (size_t)(seed % 32);
    for (size_t i = 0; i < len; i++) {
      seed = seed * 1103515245 + 12345;
      buf[i] = (uint8_t)(seed >> 16);
    }
    cerco_wreader_init(&r, buf, len);
    while (cerco_r_val(&r, &out) && r.pos < len) {}
    CHECK(r.pos <= len + 1 || r.err);
  }
}

TEST(sha256_known) {
  /* echo -n "abc" | sha256sum */
  char hex[65];
  cerco_sha256_hex("abc", 3, hex);
  CHECK_STR_EQ(hex,
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  cerco_sha256_hex("", 0, hex);
  CHECK_STR_EQ(hex,
               "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  /* 1,000,000 x 'a' */
  cerco_sha256_ctx c;
  cerco_sha256_init(&c);
  static char block[1000];
  memset(block, 'a', sizeof(block));
  for (int i = 0; i < 1000; i++) cerco_sha256_update(&c, block, sizeof(block));
  uint8_t digest[32];
  cerco_sha256_final(&c, digest);
  cerco_hex_encode(digest, 32, hex);
  CHECK_STR_EQ(hex,
               "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(mime_table) {
  CHECK_STR_EQ(cerco_mime_from_ext("app.wasm"), "application/wasm");
  CHECK_STR_EQ(cerco_mime_from_ext("style.css"), "text/css; charset=utf-8");
  CHECK_STR_EQ(cerco_mime_from_ext("index.html"), "text/html; charset=utf-8");
  CHECK_STR_EQ(cerco_mime_from_ext("photo.JPG"), "image/jpeg");
  CHECK_STR_EQ(cerco_mime_from_ext("noext"), "application/octet-stream");
}

TEST(strcaseeq) {
  CHECK(cerco_strcaseeq("GET", "get"));
  CHECK(cerco_strcaseeq("Content-Type", "content-TYPE"));
  CHECK(!cerco_strcaseeq("GET", "POST"));
  CHECK(cerco_strncaseeq("application/x-cerco-sf", "application/x", 13));
}



void main_2(void) {
  test_arena_basic();
  test_arena_hard_cap();
  test_arena_alignment();
  test_sb_basics();
  test_html_escape();
  test_url_decode();
  test_query_parse();
  test_wire_roundtrip();
  test_wire_truncated();
  test_wire_fuzz_random();
  test_sha256_known();
  test_mime_table();
  test_strcaseeq();
}
