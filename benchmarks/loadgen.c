/* cerco load generator: measures requests/sec and latency percentiles.
 * usage: loadgen <host> <port> <path|POST:/__cerco/sf/1> <conns> <duration-ms> [body-file]
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#define MAX_LATENCIES 4000000

typedef struct {
  int id;
  int conns;
  long duration_ms;
  const char *host;
  int port;
  const char *path;
  const char *post_body; /* NULL = GET */
  double *latencies;
  long count;
  long errors;
} worker_arg;

static int64_t now_ns(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static int connect_to(const char *host, int port) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  struct sockaddr_in a = {0};
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  inet_pton(AF_INET, host, &a.sin_addr);
  if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) { close(fd); return -1; }
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
  return fd;
}

static double *g_all = NULL;
static long g_all_count = 0;
static long g_all_errors = 0;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

static void *worker(void *p) {
  worker_arg *a = (worker_arg *)p;
  double *lat = malloc(sizeof(double) * (size_t)(a->duration_ms / 10 + 16));
  long n = 0;
  long errors = 0;
  int64_t deadline = now_ns() + (int64_t)a->duration_ms * 1000000LL;
  int fd = -1;
  char req[2048];
  int req_len;
  if (a->post_body) {
    req_len = snprintf(req, sizeof(req),
                       "POST %s HTTP/1.1\r\nHost: b\r\nConnection: close\r\n"
                       "Content-Type: application/x-cerco-sf\r\n"
                       "Content-Length: %zu\r\n\r\n%s",
                       a->path, strlen(a->post_body), a->post_body);
  } else {
    req_len = snprintf(req, sizeof(req),
                       "GET %s HTTP/1.1\r\nHost: b\r\nConnection: close\r\n\r\n", a->path);
  }
  char resp[16384];
  while (now_ns() < deadline) {
    int64_t t0 = now_ns();
    fd = connect_to(a->host, a->port);
    if (fd < 0) { errors++; continue; }
    if (write(fd, req, (size_t)req_len) != req_len) { errors++; close(fd); continue; }
    size_t got = 0;
    while (got < sizeof(resp) - 1) {
      ssize_t r = read(fd, resp + got, sizeof(resp) - 1 - got);
      if (r <= 0) break;
      got += (size_t)r;
    }
    close(fd);
    double ms = (double)(now_ns() - t0) / 1e6;
    /* transport failures and 5xx count as errors; 3xx/4xx are valid outcomes */
    if (strncmp(resp, "HTTP/1.1 2", 10) != 0 && strncmp(resp, "HTTP/1.1 3", 10) != 0 &&
        strncmp(resp, "HTTP/1.1 4", 10) != 0) {
      errors++;
    }
    if (n < a->duration_ms / 10 + 16) lat[n++] = ms;
  }
  if (fd >= 0) close(fd);
  pthread_mutex_lock(&g_mu);
  for (long i = 0; i < n; i++) {
    if (g_all_count < MAX_LATENCIES) g_all[g_all_count++] = lat[i];
  }
  g_all_errors += errors;
  pthread_mutex_unlock(&g_mu);
  free(lat);
  a->count = n;
  a->errors = errors;
  (void)errors;
  return NULL;
}

static int cmp_double(const void *x, const void *y) {
  double a = *(const double *)x, b = *(const double *)y;
  return a < b ? -1 : a > b ? 1 : 0;
}

int main(int argc, char **argv) {
  if (argc < 6) {
    fprintf(stderr, "usage: %s host port path conns duration-ms [post-body]\n", argv[0]);
    return 1;
  }
  const char *host = argv[1];
  int port = atoi(argv[2]);
  const char *path = argv[3];
  int conns = atoi(argv[4]);
  long duration = atol(argv[5]);
  const char *post_body = argc > 6 ? argv[6] : NULL;
  if (conns < 1) conns = 1;
  if (conns > 1000) conns = 1000;

  g_all = malloc(sizeof(double) * (size_t)(duration / 10 + 16) * (size_t)conns * 2);
  pthread_t *threads = calloc((size_t)conns, sizeof(pthread_t));
  worker_arg *args = calloc((size_t)conns, sizeof(worker_arg));

  int64_t t0 = now_ns();
  for (int i = 0; i < conns; i++) {
    args[i].id = i;
    args[i].conns = conns;
    args[i].duration_ms = duration;
    args[i].host = host;
    args[i].port = port;
    args[i].path = path;
    args[i].post_body = post_body;
    pthread_create(&threads[i], NULL, worker, &args[i]);
  }
  long total = 0, errors = 0;
  for (int i = 0; i < conns; i++) {
    pthread_join(threads[i], NULL);
    total += args[i].count;
    errors += args[i].errors;
  }
  double wall = (double)(now_ns() - t0) / 1e9;

  qsort(g_all, (size_t)g_all_count, sizeof(double), cmp_double);
  double p50 = g_all_count ? g_all[g_all_count / 2] : 0;
  double p95 = g_all_count ? g_all[(long)(g_all_count * 0.95)] : 0;
  double p99 = g_all_count ? g_all[(long)(g_all_count * 0.99)] : 0;
  double avg = 0;
  for (long i = 0; i < g_all_count; i++) avg += g_all[i];
  if (g_all_count) avg /= (double)g_all_count;

  printf("requests=%ld errors=%ld rps=%.0f avg=%.2fms p50=%.2fms p95=%.2fms p99=%.2fms wall=%.1fs\n",
         total, errors, (double)total / wall, avg, p50, p95, p99, wall);
  free(g_all);
  free(threads);
  free(args);
  return 0;
}
