/* Functional tests for mhc library.
 * Starts Python server, runs upload/download/POST, exits 0 on pass. */
#include "mhc.h"
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define SERVER_PORT  "8443"
#define UPLOAD_URL   "https://127.0.0.1:8443/upload/test5m.bin"
#define DOWNLOAD_URL "https://127.0.0.1:8443/download/test5m.bin"
#define ECHO_URL     "https://127.0.0.1:8443/echo"
#define TESTFILE     "/tmp/mhc_upload_test.bin"
#define DLFILE       "/tmp/mhc_download_test.bin"
#define FILE_SIZE    (5 * 1024 * 1024)

#define PYTHON "/home/wjx/agent_eyes/bot/http/venv/bin/python3"
#define SERVER "/home/wjx/agent_eyes/bot/http/mg_http_client/test/server.py"
#define CERT   "/home/wjx/agent_eyes/bot/http/mg_http_client/certs/web.pem"
#define KEY    "/home/wjx/agent_eyes/bot/http/mg_http_client/certs/web-key.pem"

static pid_t server_pid;

static void stop_server(void) {
  if (server_pid > 0) {
    kill(server_pid, SIGTERM);
    waitpid(server_pid, NULL, 0);
  }
}

static void start_server(void) {
  server_pid = fork();
  assert(server_pid >= 0);
  if (server_pid == 0) {
    execl(PYTHON, "python3", SERVER, SERVER_PORT, CERT, KEY, NULL);
    _exit(1);
  }
  atexit(stop_server);
  usleep(600000);  /* 600 ms: wait for server to bind */
}

static void gen_testfile(void) {
  FILE *f = fopen(TESTFILE, "wb");
  assert(f);
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

/* --- upload test ---------------------------------------------------------- */
static void on_upload(int status, void *ud) { *(int *)ud = status; }

static void test_upload(void) {
  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  mg_log_set(0);

  int status = 0;
  mhc_upload(&mgr, UPLOAD_URL, TESTFILE, on_upload, &status);
  while (status == 0) mg_mgr_poll(&mgr, 1);
  mg_mgr_free(&mgr);

  printf("upload status: %d\n", status);
  assert(status == 200);
  printf("PASS: upload\n");
}

/* --- download test -------------------------------------------------------- */
static void on_download(int status, void *ud) { *(int *)ud = status; }

static void test_download(void) {
  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  mg_log_set(0);

  int status = 0;
  mhc_download(&mgr, DOWNLOAD_URL, DLFILE, on_download, &status);
  while (status == 0) mg_mgr_poll(&mgr, 1);
  mg_mgr_free(&mgr);

  printf("download status: %d\n", status);
  assert(status == 200);

  struct stat st;
  assert(stat(DLFILE, &st) == 0);
  assert((size_t)st.st_size == FILE_SIZE);
  printf("PASS: download (%zu bytes)\n", (size_t)st.st_size);
}

/* --- POST echo test ------------------------------------------------------- */
static void on_post(int status, void *ud) { *(int *)ud = status; }

static void test_post(void) {
  struct mg_mgr mgr;
  mg_mgr_init(&mgr);
  mg_log_set(0);

  const char *body = "{\"ping\":\"pong\"}";
  int status = 0;
  mhc_post(&mgr, ECHO_URL, "application/json",
           body, strlen(body), on_post, &status);
  while (status == 0) mg_mgr_poll(&mgr, 1);
  mg_mgr_free(&mgr);

  printf("post status: %d\n", status);
  assert(status == 200);
  printf("PASS: post\n");
}

int main(void) {
  start_server();
  gen_testfile();
  test_upload();
  test_download();
  test_post();
  printf("\nAll tests PASSED\n");
  return 0;
}
