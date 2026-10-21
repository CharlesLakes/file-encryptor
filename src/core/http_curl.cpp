/**
 * @file    http_curl.cpp
 * @brief   libcurl implementation of the HTTP abstraction (see http.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * libcurl is loaded on first use; see deps/curl.h for why it is not linked and
 * why every option goes through a typed wrapper.
 */

#include "core/http.h"

#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "core/fs.h"
#include "deps/curl.h"

namespace crls{

static curl_api api;
static bool loaded = false, load_failed = false;

/**
 * @brief  Loads libcurl and initializes it, once.
 * @return True if libcurl is usable.
 */
static bool load_curl(void){
  if(loaded || load_failed) return loaded;
  void *lib = dlopen("libcurl.so.4", RTLD_NOW);
  if(!lib) lib = dlopen("libcurl-gnutls.so.4", RTLD_NOW);
  if(!lib){
    load_failed = true;
    return false;
  }
  /* POSIX allows converting the void* of dlsym() to a function pointer. */
  *(void **) &api.global_init = dlsym(lib, "curl_global_init");
  *(void **) &api.easy_init = dlsym(lib, "curl_easy_init");
  *(void **) &api.easy_setopt = dlsym(lib, "curl_easy_setopt");
  *(void **) &api.easy_perform = dlsym(lib, "curl_easy_perform");
  *(void **) &api.easy_getinfo = dlsym(lib, "curl_easy_getinfo");
  *(void **) &api.easy_cleanup = dlsym(lib, "curl_easy_cleanup");
  *(void **) &api.easy_strerror = dlsym(lib, "curl_easy_strerror");
  *(void **) &api.slist_append = dlsym(lib, "curl_slist_append");
  *(void **) &api.slist_free_all = dlsym(lib, "curl_slist_free_all");
  if(!api.global_init || !api.easy_init || !api.easy_setopt || !api.easy_perform || !api.easy_getinfo ||
     !api.easy_cleanup || !api.easy_strerror || !api.slist_append || !api.slist_free_all ||
     api.global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK){
    load_failed = true;
    return false;
  }
  loaded = true;
  return true;
}

/* ---------------- Typed option wrappers ---------------- */

/** @brief Sets an option that takes a long. */
static CURLcode opt_long(CURL *h, int option, long value){
  return api.easy_setopt(h, option, value);
}

/** @brief Sets an option that takes a data pointer or a string. */
static CURLcode opt_ptr(CURL *h, int option, const void *value){
  return api.easy_setopt(h, option, value);
}

/** @brief Sets an option that takes a curl_off_t. */
static CURLcode opt_off(CURL *h, int option, curl_off_t value){
  return api.easy_setopt(h, option, value);
}

/** @brief Sets a write or header callback. */
static CURLcode opt_write_fn(CURL *h, int option, curl_write_callback fn){
  return api.easy_setopt(h, option, fn);
}

/** @brief Sets a read callback. */
static CURLcode opt_read_fn(CURL *h, int option, curl_read_callback fn){
  return api.easy_setopt(h, option, fn);
}

/* ---------------- Callbacks ---------------- */

/** @brief State shared by the callbacks of one request. */
typedef struct{
  const http_request_spec *req;        /**< Request. */
  http_response *resp;                 /**< Response being filled. */
  uint64_t body_sent;                  /**< Bytes of the POST body already read. */
}transfer;

/**
 * @brief  Copies the value of a header line if its name matches.
 * @param  line     Header line (not terminated, may end in CRLF).
 * @param  len      Length of line.
 * @param  name     Header name followed by ':', e.g. "ETag:".
 * @param  out      Destination buffer.
 * @param  out_size Capacity of out.
 */
static void capture_header(const char *line, size_t len, const char *name, char *out, size_t out_size){
  size_t name_len = strlen(name);
  if(len < name_len || strncasecmp(line, name, name_len) != 0) return;
  const char *v = line + name_len, *end = line + len;
  while(v < end && (*v == ' ' || *v == '\t')) v++;
  while(end > v && (end[-1] == '\r' || end[-1] == '\n' || end[-1] == ' ')) end--;
  size_t n = (size_t) (end - v) < out_size - 1 ? (size_t) (end - v) : out_size - 1;
  memcpy(out, v, n);
  out[n] = '\0';
}

/** @brief CURLOPT_HEADERFUNCTION: keeps Content-Range and ETag of the final response. */
static size_t on_header(char *line, size_t size, size_t nitems, void *userdata){
  transfer *t = (transfer *) userdata;
  size_t len = size * nitems;
  /* A new status line (after "100 Continue") starts a new set of headers. */
  if(len >= 5 && strncmp(line, "HTTP/", 5) == 0){
    t->resp->content_range[0] = '\0';
    t->resp->etag[0] = '\0';
  }
  capture_header(line, len, "Content-Range:", t->resp->content_range, sizeof(t->resp->content_range));
  capture_header(line, len, "ETag:", t->resp->etag, sizeof(t->resp->etag));
  return len;
}

/** @brief CURLOPT_WRITEFUNCTION: passes the body to the sink. */
static size_t on_body(char *ptr, size_t size, size_t nmemb, void *userdata){
  transfer *t = (transfer *) userdata;
  size_t len = size * nmemb;
  if(t->req->sink && !t->req->sink(t->req->sink_ctx, ptr, len)) return 0;
  return len;
}

/** @brief CURLOPT_READFUNCTION: reads the next part of the POST body. */
static size_t on_read(char *buffer, size_t size, size_t nitems, void *userdata){
  transfer *t = (transfer *) userdata;
  uint64_t left = t->req->body_len - t->body_sent;
  size_t n = size * nitems < left ? size * nitems : (size_t) left;
  if(n > 0 && !read_at(t->req->body_fd, buffer, n, t->body_sent)) return 0;
  t->body_sent += n;
  return n;
}

bool http_perform(const http_request_spec *req, http_response *resp){
  memset(resp, 0, sizeof(*resp));
  if(!load_curl()){
    snprintf(resp->error, sizeof(resp->error), "libcurl (libcurl.so.4) is not installed.");
    return false;
  }
  CURL *h = api.easy_init();
  if(!h){
    snprintf(resp->error, sizeof(resp->error), "Could not start libcurl.");
    return false;
  }

  transfer t = {req, resp, 0};
  struct curl_slist *headers = NULL;
  for(int i = 0; req->headers && req->headers[i]; i++) headers = api.slist_append(headers, req->headers[i]);
  char error[CURL_ERROR_SIZE] = "";

  opt_ptr(h, CURLOPT_URL, req->url);
  opt_ptr(h, CURLOPT_ERRORBUFFER, error);
  if(opt_ptr(h, CURLOPT_PROTOCOLS_STR, "http,https") == CURLE_UNKNOWN_OPTION)
    opt_long(h, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
  opt_long(h, CURLOPT_FOLLOWLOCATION, 0L);
  opt_long(h, CURLOPT_NOSIGNAL, 1L);
  opt_long(h, CURLOPT_CONNECTTIMEOUT, 15L);
  /* Abort transfers slower than 1 KB/s for 30 seconds. */
  opt_long(h, CURLOPT_LOW_SPEED_LIMIT, 1024L);
  opt_long(h, CURLOPT_LOW_SPEED_TIME, 30L);
  opt_write_fn(h, CURLOPT_HEADERFUNCTION, on_header);
  opt_ptr(h, CURLOPT_HEADERDATA, &t);
  opt_write_fn(h, CURLOPT_WRITEFUNCTION, on_body);
  opt_ptr(h, CURLOPT_WRITEDATA, &t);
  if(headers) opt_ptr(h, CURLOPT_HTTPHEADER, headers);
  if(strcmp(req->method, "HEAD") == 0) opt_long(h, CURLOPT_NOBODY, 1L);
  if(strcmp(req->method, "POST") == 0){
    opt_long(h, CURLOPT_POST, 1L);
    opt_off(h, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t) req->body_len);
    opt_read_fn(h, CURLOPT_READFUNCTION, on_read);
    opt_ptr(h, CURLOPT_READDATA, &t);
  }

  CURLcode code = api.easy_perform(h);
  long status = 0;
  api.easy_getinfo(h, CURLINFO_RESPONSE_CODE, &status);
  resp->status = (int) status;
  if(code != CURLE_OK)
    snprintf(resp->error, sizeof(resp->error), "%s", error[0] ? error : api.easy_strerror(code));
  api.easy_cleanup(h);
  api.slist_free_all(headers);
  return code == CURLE_OK;
}

}
