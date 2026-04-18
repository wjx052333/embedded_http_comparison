#include "mhc.h"
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MHC_TIMEOUT_MS 10000

/* ── Upload ──────────────────────────────────────────────────────────────── */

struct mhc_upload_req {
  const char   *url;
  struct mg_fd *fd;
  size_t        fsize;
  size_t        fsent;
  mhc_data_fn   cb;
  void         *ud;
  uint64_t      deadline;
};

static void upload_fn(struct mg_connection *c, int ev, void *ev_data) {
  struct mhc_upload_req *r = (struct mhc_upload_req *) c->fn_data;
  if (r == NULL) return;

  if (ev == MG_EV_OPEN) {
    r->deadline = mg_millis() + MHC_TIMEOUT_MS;

  } else if (ev == MG_EV_POLL) {
    if (mg_millis() > r->deadline && (c->is_connecting || c->is_resolving))
      mg_error(c, "connect timeout");

  } else if (ev == MG_EV_CONNECT) {
    if (c->is_tls) {
      struct mg_tls_opts tls = {.skip_verification = 1};
      mg_tls_init(c, &tls);
    }
    struct mg_str host = mg_url_host(r->url);
    mg_printf(c,
              "POST %s HTTP/1.0\r\n"
              "Host: %.*s\r\n"
              "Content-Type: application/octet-stream\r\n"
              "Content-Length: %lu\r\n"
              "\r\n",
              mg_url_uri(r->url), (int) host.len, host.buf,
              (unsigned long) r->fsize);

  } else if (ev == MG_EV_WRITE) {
    /* Backpressure: only read more when send buffer has room */
    if (r->fsent < r->fsize && c->send.len < MG_IO_SIZE) {
      uint8_t buf[MG_IO_SIZE];
      size_t n = r->fsize - r->fsent;
      if (n > MG_IO_SIZE) n = MG_IO_SIZE;
      r->fd->fs->rd(r->fd->fd, buf, n);
      mg_send(c, buf, n);
      r->fsent += n;
    }

  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    int status = mg_http_status(hm);
    mg_fs_close(r->fd);
    mhc_data_fn cb = r->cb;
    void *ud = r->ud;
    free(r);
    c->fn_data = NULL;
    c->is_draining = 1;
    cb(status, ud);

  } else if (ev == MG_EV_ERROR) {
    mg_fs_close(r->fd);
    mhc_data_fn cb = r->cb;
    void *ud = r->ud;
    free(r);
    c->fn_data = NULL;
    if (cb) cb(-1, ud);
  }
  (void) ev_data;
}

void mhc_upload(struct mg_mgr *mgr, const char *url, const char *filepath,
                mhc_data_fn cb, void *userdata) {
  size_t fsize = 0;
  time_t mtime;
  mg_fs_posix.st(filepath, &fsize, &mtime);
  struct mg_fd *fd = mg_fs_open(&mg_fs_posix, filepath, MG_FS_READ);
  if (fd == NULL || fsize == 0) {
    if (cb) cb(-1, userdata);
    return;
  }
  struct mhc_upload_req *r = calloc(1, sizeof(*r));
  r->url = url;
  r->fd = fd;
  r->fsize = fsize;
  r->cb = cb;
  r->ud = userdata;
  mg_http_connect(mgr, url, upload_fn, r);
}

/* ── Download ────────────────────────────────────────────────────────────── */

struct mhc_dl_req {
  const char   *url;
  const char   *savepath;
  struct mg_fd *fd;
  bool          hdrs_parsed;
  mhc_data_fn   cb;
  void         *ud;
  uint64_t      deadline;
};

static void download_fn(struct mg_connection *c, int ev, void *ev_data) {
  struct mhc_dl_req *r = (struct mhc_dl_req *) c->fn_data;
  if (r == NULL) return;

  if (ev == MG_EV_OPEN) {
    r->deadline = mg_millis() + MHC_TIMEOUT_MS;

  } else if (ev == MG_EV_POLL) {
    if (mg_millis() > r->deadline && (c->is_connecting || c->is_resolving))
      mg_error(c, "connect timeout");

  } else if (ev == MG_EV_CONNECT) {
    if (mg_url_is_ssl(r->url)) {
      struct mg_tls_opts tls = {.skip_verification = 1};
      mg_tls_init(c, &tls);
    }
    struct mg_str host = mg_url_host(r->url);
    mg_printf(c, "GET %s HTTP/1.0\r\nHost: %.*s\r\n\r\n",
              mg_url_uri(r->url), (int) host.len, host.buf);

  } else if (ev == MG_EV_READ) {
    if (!r->hdrs_parsed) {
      /* Parse response headers from raw bytes */
      struct mg_http_message hm;
      int n = mg_http_parse((char *) c->recv.buf, c->recv.len, &hm);
      if (n < 0) { mg_error(c, "bad HTTP response"); return; }
      if (n == 0) return;  /* need more data */
      int status = mg_http_status(&hm);
      if (status != 200) {
        mhc_data_fn cb = r->cb;
        void *ud = r->ud;
        free(r);
        c->fn_data = NULL;
        c->is_draining = 1;
        cb(status, ud);
        return;
      }
      mg_fs_posix.rm(r->savepath);  /* truncate: remove before write */
      r->fd = mg_fs_open(&mg_fs_posix, r->savepath, MG_FS_WRITE);
      r->hdrs_parsed = true;
      /* Write any body bytes that arrived alongside the headers */
      size_t body_offset = (size_t) n;
      if (c->recv.len > body_offset && r->fd)
        r->fd->fs->wr(r->fd->fd, c->recv.buf + body_offset,
                      c->recv.len - body_offset);
      c->recv.len = 0;
    } else {
      /* Pure body chunk */
      if (r->fd) r->fd->fs->wr(r->fd->fd, c->recv.buf, c->recv.len);
      c->recv.len = 0;
    }

  } else if (ev == MG_EV_CLOSE) {
    /* HTTP/1.0: server closes connection after response — that's our EOF */
    int status = r->hdrs_parsed ? 200 : -1;
    if (r->fd) mg_fs_close(r->fd);
    mhc_data_fn cb = r->cb;
    void *ud = r->ud;
    free(r);
    c->fn_data = NULL;
    cb(status, ud);

  } else if (ev == MG_EV_ERROR) {
    if (r->fd) mg_fs_close(r->fd);
    mhc_data_fn cb = r->cb;
    void *ud = r->ud;
    free(r);
    c->fn_data = NULL;
    cb(-1, ud);
  }
  (void) ev_data;
}

void mhc_download(struct mg_mgr *mgr, const char *url, const char *savepath,
                  mhc_data_fn cb, void *userdata) {
  struct mhc_dl_req *r = calloc(1, sizeof(*r));
  r->url = url;
  r->savepath = savepath;
  r->cb = cb;
  r->ud = userdata;
  mg_connect(mgr, url, download_fn, r);
}

/* ── POST ────────────────────────────────────────────────────────────────── */

struct mhc_post_req {
  const char  *url;
  const void  *body;
  size_t       body_len;
  const char  *ct;
  mhc_data_fn  cb;
  void        *ud;
  uint64_t     deadline;
};

static void post_fn(struct mg_connection *c, int ev, void *ev_data) {
  struct mhc_post_req *r = (struct mhc_post_req *) c->fn_data;
  if (r == NULL) return;

  if (ev == MG_EV_OPEN) {
    r->deadline = mg_millis() + MHC_TIMEOUT_MS;

  } else if (ev == MG_EV_POLL) {
    if (mg_millis() > r->deadline && (c->is_connecting || c->is_resolving))
      mg_error(c, "connect timeout");

  } else if (ev == MG_EV_CONNECT) {
    if (c->is_tls) {
      struct mg_tls_opts tls = {.skip_verification = 1};
      mg_tls_init(c, &tls);
    }
    struct mg_str host = mg_url_host(r->url);
    mg_printf(c,
              "POST %s HTTP/1.0\r\n"
              "Host: %.*s\r\n"
              "Content-Type: %s\r\n"
              "Content-Length: %lu\r\n"
              "\r\n",
              mg_url_uri(r->url), (int) host.len, host.buf,
              r->ct, (unsigned long) r->body_len);
    /* mg_send copies into the send buffer — body pointer no longer needed */
    mg_send(c, r->body, r->body_len);

  } else if (ev == MG_EV_HTTP_MSG) {
    struct mg_http_message *hm = (struct mg_http_message *) ev_data;
    int status = mg_http_status(hm);
    mhc_data_fn cb = r->cb;
    void *ud = r->ud;
    free(r);
    c->fn_data = NULL;
    c->is_draining = 1;
    cb(status, ud);

  } else if (ev == MG_EV_ERROR) {
    mhc_data_fn cb = r->cb;
    void *ud = r->ud;
    free(r);
    c->fn_data = NULL;
    cb(-1, ud);
  }
  (void) ev_data;
}

void mhc_post(struct mg_mgr *mgr, const char *url, const char *content_type,
              const void *body, size_t body_len,
              mhc_data_fn cb, void *userdata) {
  struct mhc_post_req *r = calloc(1, sizeof(*r));
  r->url = url;
  r->ct = content_type;
  r->body = body;
  r->body_len = body_len;
  r->cb = cb;
  r->ud = userdata;
  mg_http_connect(mgr, url, post_fn, r);
}
