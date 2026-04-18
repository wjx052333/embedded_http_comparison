/* Benchmark: 100× upload + 100× download of a 5 MB file.
 * Measures p50/p95/p99 latency, throughput MB/s, peak RSS, CPU time.
 * Writes a Markdown report to docs/bench_report.md. */
#include "mhc.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define ITERATIONS   100
#define FILE_SIZE    (5 * 1024 * 1024)
#define SERVER_PORT  8444
#define TESTFILE     "/tmp/mhc_bench_upload.bin"
#define DLFILE       "/tmp/mhc_bench_download.bin"
#define UPLOAD_URL   "https://127.0.0.1:8444/upload/bench.bin"
#define DOWNLOAD_URL "https://127.0.0.1:8444/download/bench.bin"
#define PYTHON       "/home/wjx/agent_eyes/bot/http/venv/bin/python3"
#define SERVER       "/home/wjx/agent_eyes/bot/http/mg_http_client/test/server.py"
#define CERT         "/home/wjx/agent_eyes/bot/http/mg_http_client/certs/web.pem"
#define KEY          "/home/wjx/agent_eyes/bot/http/mg_http_client/certs/web-key.pem"
#define REPORT_PATH  "/home/wjx/agent_eyes/bot/http/mg_http_client/docs/bench_report.md"

static pid_t server_pid;

static void stop_server(void) {
  if (server_pid > 0) { kill(server_pid, SIGTERM); waitpid(server_pid, NULL, 0); }
}

static void start_server(void) {
  server_pid = fork();
  if (server_pid == 0) {
    char port[8]; snprintf(port, sizeof(port), "%d", SERVER_PORT);
    execl(PYTHON, "python3", SERVER, port, CERT, KEY, NULL);
    _exit(1);
  }
  atexit(stop_server);
  usleep(600000);
}

static void gen_testfile(void) {
  FILE *f = fopen(TESTFILE, "wb");
  if (!f) { perror("fopen"); exit(1); }
  srand(42);
  uint8_t buf[4096];
  for (size_t i = 0; i < sizeof(buf); i++) buf[i] = (uint8_t)(rand() & 0xff);
  size_t written = 0;
  while (written < FILE_SIZE) {
    size_t n = FILE_SIZE - written;
    if (n > sizeof(buf)) n = sizeof(buf);
    fwrite(buf, 1, n, f);
    written += n;
  }
  fclose(f);
}

/* RSS from /proc/self/status, kB */
static long rss_kb(void) {
  FILE *f = fopen("/proc/self/status", "r");
  if (!f) return 0;
  char line[128];
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "VmRSS:", 6) == 0) {
      fclose(f);
      long v = 0; sscanf(line + 6, "%ld", &v); return v;
    }
  }
  fclose(f); return 0;
}

static int cmp_double(const void *a, const void *b) {
  double da = *(const double *)a, db = *(const double *)b;
  return (da > db) - (da < db);
}

static void on_done(int status, void *ud) { *(int *)ud = status; }

/* Run one upload, return elapsed ms */
static double run_upload(struct mg_mgr *mgr) {
  int status = 0;
  uint64_t t0 = mg_millis();
  mhc_upload(mgr, UPLOAD_URL, TESTFILE, on_done, &status);
  while (status == 0) mg_mgr_poll(mgr, 1);
  return (double)(mg_millis() - t0);
}

/* Run one download, return elapsed ms */
static double run_download(struct mg_mgr *mgr) {
  int status = 0;
  uint64_t t0 = mg_millis();
  mhc_download(mgr, DOWNLOAD_URL, DLFILE, on_done, &status);
  while (status == 0) mg_mgr_poll(mgr, 1);
  return (double)(mg_millis() - t0);
}

static void print_stats(const char *label, double *ms, int n,
                        double mb_total, FILE *out) {
  qsort(ms, (size_t) n, sizeof(double), cmp_double);
  double sum = 0;
  for (int i = 0; i < n; i++) sum += ms[i];
  double avg_ms    = sum / n;
  double total_s   = sum / 1000.0;
  double throughput = mb_total / total_s;  /* MB/s */

  fprintf(out, "### %s (%d x %.1f MB)\n\n", label, n, (double)FILE_SIZE/(1024*1024));
  fprintf(out, "| Metric | Value |\n|--------|-------|\n");
  fprintf(out, "| p50 latency | %.1f ms |\n", ms[n / 2]);
  fprintf(out, "| p95 latency | %.1f ms |\n", ms[(int)(n * 0.95)]);
  fprintf(out, "| p99 latency | %.1f ms |\n", ms[(int)(n * 0.99)]);
  fprintf(out, "| avg latency | %.1f ms |\n", avg_ms);
  fprintf(out, "| min latency | %.1f ms |\n", ms[0]);
  fprintf(out, "| max latency | %.1f ms |\n", ms[n - 1]);
  fprintf(out, "| throughput  | %.2f MB/s |\n\n", throughput);
}

int main(void) {
  printf("Starting server on port %d...\n", SERVER_PORT);
  start_server();
  gen_testfile();

  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  mg_log_set(0);

  double up_ms[ITERATIONS], dl_ms[ITERATIONS];
  long rss_baseline = rss_kb();

  /* warm-up: one upload so file exists on server for download */
  { int s = 0; mhc_upload(&mgr, UPLOAD_URL, TESTFILE, on_done, &s);
    while (!s) mg_mgr_poll(&mgr, 1); }

  /* upload benchmark */
  printf("Running %d uploads...\n", ITERATIONS);
  for (int i = 0; i < ITERATIONS; i++) {
    up_ms[i] = run_upload(&mgr);
    if (i % 10 == 9) printf("  upload %d done (%.0f ms)\n", i+1, up_ms[i]);
  }
  long rss_after_up = rss_kb();

  /* download benchmark */
  printf("Running %d downloads...\n", ITERATIONS);
  for (int i = 0; i < ITERATIONS; i++) {
    dl_ms[i] = run_download(&mgr);
    if (i % 10 == 9) printf("  download %d done (%.0f ms)\n", i+1, dl_ms[i]);
  }
  long rss_after_dl = rss_kb();

  mg_mgr_free(&mgr);

  /* CPU time */
  struct rusage ru;
  getrusage(RUSAGE_SELF, &ru);
  double cpu_user = (double)ru.ru_utime.tv_sec + ru.ru_utime.tv_usec / 1e6;
  double cpu_sys  = (double)ru.ru_stime.tv_sec + ru.ru_stime.tv_usec / 1e6;

  long rss_peak = rss_after_up > rss_after_dl ? rss_after_up : rss_after_dl;

  /* Write report */
  FILE *rpt = fopen(REPORT_PATH, "w");
  if (!rpt) rpt = stdout;

  fprintf(rpt, "# mg_http_client Benchmark Report\n\n");
  fprintf(rpt, "**Date:** %s %s\n", __DATE__, __TIME__);
  fprintf(rpt, "**File size:** %.1f MB  **Iterations:** %d\n\n",
          (double)FILE_SIZE/(1024*1024), ITERATIONS);

  double up_total_mb = (double)ITERATIONS * FILE_SIZE / (1024.0*1024.0);
  double up_ms_copy[ITERATIONS], dl_ms_copy[ITERATIONS];
  memcpy(up_ms_copy, up_ms, sizeof(up_ms));
  memcpy(dl_ms_copy, dl_ms, sizeof(dl_ms));
  print_stats("Upload",   up_ms_copy, ITERATIONS, up_total_mb, rpt);
  print_stats("Download", dl_ms_copy, ITERATIONS, up_total_mb, rpt);

  fprintf(rpt, "### System Resources\n\n");
  fprintf(rpt, "| Metric | Value |\n|--------|-------|\n");
  fprintf(rpt, "| RSS baseline | %ld kB |\n", rss_baseline);
  fprintf(rpt, "| RSS after uploads | %ld kB |\n", rss_after_up);
  fprintf(rpt, "| RSS after downloads | %ld kB |\n", rss_after_dl);
  fprintf(rpt, "| RSS peak | %ld kB |\n", rss_peak);
  fprintf(rpt, "| CPU user time | %.3f s |\n", cpu_user);
  fprintf(rpt, "| CPU sys time  | %.3f s |\n", cpu_sys);
  fprintf(rpt, "\n");

  if (rpt != stdout) {
    fclose(rpt);
    printf("\nReport written to %s\n", REPORT_PATH);
  }

  /* Also print summary to stdout */
  {
    double up_copy[ITERATIONS], dl_copy[ITERATIONS];
    memcpy(up_copy, up_ms, sizeof(up_ms));
    memcpy(dl_copy, dl_ms, sizeof(dl_ms));
    print_stats("Upload",   up_copy, ITERATIONS, up_total_mb, stdout);
    print_stats("Download", dl_copy, ITERATIONS, up_total_mb, stdout);
  }
  printf("RSS peak: %ld kB  CPU: user=%.3fs sys=%.3fs\n",
         rss_peak, cpu_user, cpu_sys);
  return 0;
}
