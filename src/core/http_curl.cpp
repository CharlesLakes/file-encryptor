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

#include <algorithm>
#include <array>
#include <memory>
#include <dlfcn.h>
#include "core/fs.h"
#include "core/util.h"
#include "deps/curl.h"

namespace crls{

namespace{

/* Filled by load_curl() the first time a request is made. */
curl_api api;

/**
 * @brief  Looks up a symbol of libcurl and stores it as a function pointer.
 * @param  lib  Handle from dlopen().
 * @param  name Symbol name.
 * @param  out  Output: the function pointer.
 * @return True if the symbol exists.
 */
template<class F>
bool resolve(void *lib, const char *name, F &out){
  void *p = dlsym(lib, name);
  /* POSIX guarantees that the void* of dlsym() converts to a function pointer. */
  out = reinterpret_cast<F>(p);
  return p != nullptr;
}

/**
 * @brief  Loads libcurl and initializes it. Only the first call does the work
 *         (a function-local static is initialized exactly once).
 * @return True if libcurl is usable.
 */
bool load_curl(){
  static const bool loaded = []{
    void *lib = dlopen("libcurl.so.4", RTLD_NOW);
    if(!lib) lib = dlopen("libcurl-gnutls.so.4", RTLD_NOW);
    return lib && resolve(lib, "curl_global_init", api.global_init) && resolve(lib, "curl_easy_init", api.easy_init) &&
           resolve(lib, "curl_easy_setopt", api.easy_setopt) && resolve(lib, "curl_easy_perform", api.easy_perform) &&
           resolve(lib, "curl_easy_getinfo", api.easy_getinfo) && resolve(lib, "curl_easy_cleanup", api.easy_cleanup) &&
           resolve(lib, "curl_easy_strerror", api.easy_strerror) && resolve(lib, "curl_slist_append", api.slist_append) &&
           resolve(lib, "curl_slist_free_all", api.slist_free_all) && api.global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
  }();
  return loaded;
}

/** @brief Deleter of an easy handle. */
struct easy_deleter{
  void operator()(CURL *h) const noexcept{ api.easy_cleanup(h); }
};

/** @brief Deleter of a header list. */
struct slist_deleter{
  void operator()(curl_slist *list) const noexcept{ api.slist_free_all(list); }
};

using easy_handle = std::unique_ptr<CURL, easy_deleter>;
using header_list = std::unique_ptr<curl_slist, slist_deleter>;

/* ---------------- Typed option wrappers ---------------- */

/** @brief Sets an option that takes a long. */
CURLcode opt_long(CURL *h, int option, long value){
  return api.easy_setopt(h, option, value);
}

/** @brief Sets an option that takes a data pointer or a string. */
CURLcode opt_ptr(CURL *h, int option, const void *value){
  return api.easy_setopt(h, option, value);
}

/** @brief Sets an option that takes a curl_off_t. */
CURLcode opt_off(CURL *h, int option, curl_off_t value){
  return api.easy_setopt(h, option, value);
}

/** @brief Sets a write or header callback. */
CURLcode opt_write_fn(CURL *h, int option, curl_write_callback fn){
  return api.easy_setopt(h, option, fn);
}

/** @brief Sets a read callback. */
CURLcode opt_read_fn(CURL *h, int option, curl_read_callback fn){
  return api.easy_setopt(h, option, fn);
}

/* ---------------- Callbacks ---------------- */

/** @brief State shared by the callbacks of one request. */
struct transfer{
  const http_request_spec &req;        /**< Request. */
  http_response &resp;                 /**< Response being filled. */
  uint64_t body_sent = 0;              /**< Bytes of the POST body already read. */
};

/**
 * @brief  Returns the value of a header line if its name matches.
 * @param  line Header line (may end in CRLF).
 * @param  name Header name followed by ':', e.g. "ETag:".
 * @return The value without surrounding blanks, or std::nullopt if the name
 *         does not match.
 */
std::optional<std::string_view> header_value(std::string_view line, std::string_view name){
  if(!starts_with_nocase(line, name)) return std::nullopt;
  std::string_view value = line.substr(name.size());
  while(value.ends_with('\r') || value.ends_with('\n')) value.remove_suffix(1);
  return trim(value);
}

/** @brief CURLOPT_HEADERFUNCTION: keeps Content-Range and ETag of the final response. */
size_t on_header(char *line, size_t size, size_t nitems, void *userdata){
  transfer *t = static_cast<transfer *>(userdata);
  std::string_view text(line, size * nitems);
  /* A new status line (after "100 Continue") starts a new set of headers. */
  if(text.starts_with("HTTP/")){
    t->resp.content_range.clear();
    t->resp.etag.clear();
  }
  if(auto v = header_value(text, "Content-Range:")) t->resp.content_range = *v;
  if(auto v = header_value(text, "ETag:")) t->resp.etag = *v;
  return text.size();
}

/** @brief CURLOPT_WRITEFUNCTION: passes the body to the sink. */
size_t on_body(char *ptr, size_t size, size_t nmemb, void *userdata){
  transfer *t = static_cast<transfer *>(userdata);
  size_t len = size * nmemb;
  if(t->req.sink && !t->req.sink({reinterpret_cast<const unsigned char *>(ptr), len})) return 0;
  return len;
}

/** @brief CURLOPT_READFUNCTION: reads the next part of the POST body. */
size_t on_read(char *buffer, size_t size, size_t nitems, void *userdata){
  transfer *t = static_cast<transfer *>(userdata);
  size_t n = static_cast<size_t>(std::min<uint64_t>(size * nitems, t->req.body_len - t->body_sent));
  if(n > 0 && !read_at(t->req.body_fd, {reinterpret_cast<unsigned char *>(buffer), n}, t->body_sent)) return 0;
  t->body_sent += n;
  return n;
}

}

bool http_perform(const http_request_spec &req, http_response &resp){
  resp = http_response();
  if(!load_curl()){
    resp.error = "libcurl (libcurl.so.4) is not installed.";
    return false;
  }
  easy_handle h(api.easy_init());
  if(!h){
    resp.error = "Could not start libcurl.";
    return false;
  }

  transfer t{req, resp};
  header_list headers;
  for(const std::string &line : req.headers){
    curl_slist *grown = api.slist_append(headers.get(), line.c_str());
    if(!grown){
      resp.error = "Not enough memory.";
      return false;
    }
    headers.release();
    headers.reset(grown);
  }
  std::array<char, CURL_ERROR_SIZE> error{};

  opt_ptr(h.get(), CURLOPT_URL, req.url.c_str());
  opt_ptr(h.get(), CURLOPT_ERRORBUFFER, error.data());
  if(opt_ptr(h.get(), CURLOPT_PROTOCOLS_STR, "http,https") == CURLE_UNKNOWN_OPTION)
    opt_long(h.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
  opt_long(h.get(), CURLOPT_FOLLOWLOCATION, 0L);
  opt_long(h.get(), CURLOPT_NOSIGNAL, 1L);
  opt_long(h.get(), CURLOPT_CONNECTTIMEOUT, 15L);
  /* Abort transfers slower than 1 KB/s for 30 seconds. */
  opt_long(h.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L);
  opt_long(h.get(), CURLOPT_LOW_SPEED_TIME, 30L);
  opt_write_fn(h.get(), CURLOPT_HEADERFUNCTION, on_header);
  opt_ptr(h.get(), CURLOPT_HEADERDATA, &t);
  opt_write_fn(h.get(), CURLOPT_WRITEFUNCTION, on_body);
  opt_ptr(h.get(), CURLOPT_WRITEDATA, &t);
  if(headers) opt_ptr(h.get(), CURLOPT_HTTPHEADER, headers.get());
  if(req.method == "HEAD") opt_long(h.get(), CURLOPT_NOBODY, 1L);
  if(req.method == "POST"){
    opt_long(h.get(), CURLOPT_POST, 1L);
    opt_off(h.get(), CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(req.body_len));
    opt_read_fn(h.get(), CURLOPT_READFUNCTION, on_read);
    opt_ptr(h.get(), CURLOPT_READDATA, &t);
  }

  CURLcode code = api.easy_perform(h.get());
  long status = 0;
  api.easy_getinfo(h.get(), CURLINFO_RESPONSE_CODE, &status);
  resp.status = static_cast<int>(status);
  if(code != CURLE_OK) resp.error = error[0] ? error.data() : api.easy_strerror(code);
  return code == CURLE_OK;
}

}
