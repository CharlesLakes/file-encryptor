/**
 * @file    http.h
 * @brief   Operating system abstraction for HTTP and HTTPS requests.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section http_overview Overview
 *
 * One blocking request at a time, used by remote.cpp. The implementation is
 * selected at build time:
 *
 * | File            | Platform                                   |
 * |-----------------|--------------------------------------------|
 * | http_curl.cpp   | Linux: libcurl, loaded at run time         |
 * | http_winhttp.cpp  | Windows: WinHTTP, part of the system       |
 *
 * Both only allow http and https, verify certificates, never follow
 * redirects and never ask for compressed responses (which would change the
 * meaning of byte ranges).
 */

#ifndef CORE_HTTP_H
#define CORE_HTTP_H

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "lib/function_ref.h"

namespace crls{

/**
 * @brief Receives response body bytes; returns false to abort the request.
 */
using http_sink = function_ref<bool(std::span<const unsigned char> data)>;

/**
 * @brief A request.
 */
struct http_request_spec{
  std::string_view method = "GET";     /**< "GET", "HEAD" or "POST". */
  std::string url;                     /**< Absolute http or https URL. */
  std::vector<std::string> headers;    /**< Extra "Name: value" lines. */
  int body_fd = -1;                    /**< File with the POST body, read from offset 0, or -1. */
  uint64_t body_len = 0;               /**< Length of the POST body. */
  http_sink sink;                      /**< Receiver of the response body; empty to discard it. */
};

/**
 * @brief What is kept of a response.
 */
struct http_response{
  int status = 0;                      /**< Status code of the final response. */
  std::string content_range;           /**< Content-Range header, or empty. */
  std::string etag;                    /**< ETag header, or empty. */
  std::string error;                   /**< Transport error when the request failed, or empty. */
};

/**
 * @brief  Performs a request.
 * @param  req  Request.
 * @param  resp Output: response.
 * @return True if a response was received (whatever its status), false on a
 *         transport error (resp.error explains it).
 */
[[nodiscard]] bool http_perform(const http_request_spec &req, http_response &resp);

}

#endif
