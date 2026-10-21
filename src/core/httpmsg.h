/**
 * @file    httpmsg.h
 * @brief   Strict HTTP/1.1 message parsing and formatting helpers.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section httpmsg_overview Overview
 *
 * Pure functions with no I/O, shared by the server (requests) and the remote
 * client (Content-Range of responses). The parser deliberately accepts only a
 * small, unambiguous subset of HTTP/1.1:
 *
 * - The request line and every header line must end in CRLF and fit in
 *   HTTP_MAX_LINE bytes; at most HTTP_MAX_HEADERS headers are accepted.
 * - Transfer-Encoding is rejected, and so are repeated or malformed
 *   Content-Length headers, because both open the door to request smuggling.
 * - Range must be a single "bytes=" range.
 */

#ifndef CORE_HTTPMSG_H
#define CORE_HTTPMSG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HTTP_MAX_LINE    8192    /**< Longest accepted request or header line. */
#define HTTP_MAX_HEADERS 64      /**< Most accepted headers per request. */
#define HTTP_MAX_HEAD    16384   /**< Longest accepted request head (line and headers). */

/** @brief Result of http_parse_request(). */
typedef enum{
  HTTP_PARSE_OK,             /**< A complete, valid request head. */
  HTTP_PARSE_INCOMPLETE,     /**< More bytes are needed. */
  HTTP_PARSE_BAD,            /**< Malformed request (400). */
  HTTP_PARSE_TOO_LARGE,      /**< Line, head or header count over the limit (431). */
  HTTP_PARSE_UNSUPPORTED     /**< Transfer-Encoding or another unsupported feature (501). */
}http_parse_result;

/** @brief Result of http_parse_range(). */
typedef enum{
  HTTP_RANGE_OK,             /**< One satisfiable range. */
  HTTP_RANGE_INVALID         /**< Malformed, multiple or unsatisfiable range (416). */
}http_range_result;

/**
 * @brief One header of a request; points into the parsed buffer.
 */
typedef struct{
  const char *name;          /**< Header name (not terminated). */
  size_t name_len;           /**< Length of name. */
  const char *value;         /**< Value without surrounding spaces (not terminated). */
  size_t value_len;          /**< Length of value. */
}http_header;

/**
 * @brief A parsed request head. Its strings point into the parsed buffer,
 *        which must outlive it.
 */
typedef struct{
  char method[16];                       /**< Method, e.g. "GET". */
  char target[1024];                     /**< Request target as sent, e.g. "/a%20b.crls". */
  http_header headers[HTTP_MAX_HEADERS]; /**< Headers in order. */
  int header_count;                      /**< Number of headers. */
  bool has_length;                       /**< True if Content-Length was sent. */
  uint64_t content_length;               /**< Value of Content-Length. */
  size_t head_length;                    /**< Bytes of the head, including the blank line. */
}http_request;

/**
 * @brief  Parses the head of a request from the start of buf.
 * @param  buf Received bytes (the body may follow the head).
 * @param  len Number of received bytes.
 * @param  req Output: the request; valid only on HTTP_PARSE_OK.
 * @return Parse result.
 */
http_parse_result http_parse_request(const char *buf, size_t len, http_request *req);

/**
 * @brief  Copies the value of a header, matched case-insensitively.
 * @param  req      Parsed request.
 * @param  name     Header name.
 * @param  out      Destination buffer.
 * @param  out_size Capacity of out.
 * @return True if the header exists and fits in out, false otherwise.
 */
bool http_get_header(const http_request *req, const char *name, char *out, size_t out_size);

/**
 * @brief  Decodes the path of a request target ("/name%20x.crls" ->
 *         "name x.crls"). Queries, fragments, NUL bytes and malformed escapes
 *         are rejected.
 * @param  target   Request target, starting with '/'.
 * @param  out      Destination buffer for the decoded path without the
 *                  leading '/'.
 * @param  out_size Capacity of out.
 * @return True on success, false otherwise.
 */
bool http_decode_path(const char *target, char *out, size_t out_size);

/**
 * @brief  Parses a Range header against a resource of size bytes.
 * @param  value Header value, e.g. "bytes=0-99", "bytes=100-" or "bytes=-50".
 * @param  size  Size of the resource.
 * @param  first Output: first byte of the range.
 * @param  last  Output: last byte of the range (inclusive).
 * @return HTTP_RANGE_OK or HTTP_RANGE_INVALID.
 */
http_range_result http_parse_range(const char *value, uint64_t size, uint64_t *first, uint64_t *last);

/**
 * @brief  Parses a Content-Range header of the form "bytes first-last/total".
 * @param  value Header value.
 * @param  first Output: first byte.
 * @param  last  Output: last byte (inclusive).
 * @param  total Output: size of the whole resource.
 * @return True on success, false otherwise.
 */
bool http_parse_content_range(const char *value, uint64_t *first, uint64_t *last, uint64_t *total);

/**
 * @brief  Parses a decimal unsigned 64-bit number, rejecting signs, spaces,
 *         empty strings and overflow.
 * @param  s     String.
 * @param  len   Number of characters of s to parse.
 * @param  value Output: the number.
 * @return True on success, false otherwise.
 */
bool http_parse_u64(const char *s, size_t len, uint64_t *value);

/**
 * @brief Encodes bytes as lowercase hexadecimal.
 * @param data Bytes.
 * @param len  Number of bytes.
 * @param out  Destination buffer of 2 * len + 1 characters.
 */
void http_hex_encode(const unsigned char *data, size_t len, char *out);

/**
 * @brief  Decodes exactly len bytes of hexadecimal.
 * @param  hex Hexadecimal string of exactly 2 * len characters.
 * @param  out Destination buffer of len bytes.
 * @param  len Number of bytes.
 * @return True on success, false if hex has another length or invalid digits.
 */
bool http_hex_decode(const char *hex, unsigned char *out, size_t len);

/**
 * @brief  Returns the reason phrase of a status code, e.g. "Not Found".
 * @param  status Status code.
 * @return Reason phrase.
 */
const char* http_reason(int status);

#endif
