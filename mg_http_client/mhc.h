#pragma once
#include "mongoose.h"
#include <stddef.h>

/* Completion callback.
 * status = HTTP status code (200, 404 …) on success, or -1 on network error. */
typedef void (*mhc_data_fn)(int status, void *userdata);

/* Upload local file to URL via HTTP POST (application/octet-stream).
 * The library opens, streams, and closes filepath.
 * url and filepath must remain valid until the callback fires. */
void mhc_upload(struct mg_mgr *mgr, const char *url, const char *filepath,
                mhc_data_fn cb, void *userdata);

/* Download URL and save to savepath (creates/overwrites file).
 * The library streams the body directly to disk without buffering.
 * url and savepath must remain valid until the callback fires. */
void mhc_download(struct mg_mgr *mgr, const char *url, const char *savepath,
                  mhc_data_fn cb, void *userdata);

/* POST an in-memory body to URL.
 * body pointer only needs to be valid until mg_http_connect returns. */
void mhc_post(struct mg_mgr *mgr, const char *url, const char *content_type,
              const void *body, size_t body_len,
              mhc_data_fn cb, void *userdata);
