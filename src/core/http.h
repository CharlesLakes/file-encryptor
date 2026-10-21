/**
 * @file    http.h
 * @brief   Operating system abstraction for HTTP and HTTPS requests.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section http_overview Overview
 *
 * One blocking request at a time, used by remote.c. The implementation is
 * selected at build time:
 *
 * | File            | Platform                                   |
 * |-----------------|--------------------------------------------|
 * | http_curl.c     | Linux: libcurl, loaded at run time         |
 * | http_winhttp.c  | Windows: WinHTTP, part of the system       |
 *
 * Both only allow http and https, verify certificates, never follow
 * redirects and never ask for compressed responses (which would change the
 * meaning of byte ranges).
 */

#ifndef CORE_HTTP_H
#define CORE_HTTP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

namespace crls{

/**
 * @brief Receives response body bytes.
 * @param ctx  State of the receiver.
 * @param data Bytes.
 * @param len  Number of bytes.
 * @return True to continue, false to abort the request.
 */
typedef bool (*http_sink)(void *ctx, const void *data, size_t len);

/**
 * @brief A request. Unused fields are zero.
 */
typedef struct{
  const char *method;                  /**< "GET", "HEAD" or "POST". */
  const char *url;                     /**< Absolute http or https URL. */
  const char *const *headers;          /**< Extra "Name: value" lines, NULL-terminated, or NULL. */
  int body_fd;                         /**< File with the POST body, read from offset 0, or -1. */
  uint64_t body_len;                   /**< Length of the POST body. */
  http_sink sink;                      /**< Receiver of the response body, or NULL to discard it. */
  void *sink_ctx;                      /**< State of sink. */
}http_request_spec;

/**
 * @brief What is kept of a response.
 */
typedef struct{
  int status;                          /**< Status code of the final response. */
  char content_range[128];             /**< Content-Range header, or "". */
  char etag[128];                      /**< ETag header, or "". */
  char error[256];                     /**< Transport error when the request failed, or "". */
}http_response;

/**
 * @brief  Performs a request.
 * @param  req  Request.
 * @param  resp Output: response.
 * @return True if a response was received (whatever its status), false on a
 *         transport error (resp->error explains it).
 */
bool http_perform(const http_request_spec *req, http_response *resp);

}

#endif
