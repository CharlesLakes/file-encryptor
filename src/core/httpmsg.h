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
 *   http_max_line bytes; at most http_max_headers headers are accepted.
 * - Transfer-Encoding is rejected, and so are repeated or malformed
 *   Content-Length headers, because both open the door to request smuggling.
 * - Range must be a single "bytes=" range.
 *
 * Parsed values are std::string_view slices of the received bytes, which must
 * outlive them.
 */

#ifndef CORE_HTTPMSG_H
#define CORE_HTTPMSG_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace crls{

inline constexpr std::size_t http_max_line = 8192;      /**< Longest accepted request or header line. */
inline constexpr std::size_t http_max_headers = 64;     /**< Most accepted headers per request. */
inline constexpr std::size_t http_max_head = 16384;     /**< Longest accepted request head (line and headers). */

/** @brief Result of http_parse_request(). */
enum class http_parse{
  ok,                        /**< A complete, valid request head. */
  incomplete,                /**< More bytes are needed. */
  bad,                       /**< Malformed request (400). */
  too_large,                 /**< Line, head or header count over the limit (431). */
  unsupported                /**< Transfer-Encoding or another unsupported feature (501). */
};

/**
 * @brief One header of a request.
 */
struct http_header{
  std::string_view name;               /**< Header name. */
  std::string_view value;              /**< Value without surrounding spaces. */
};

/**
 * @brief A parsed request head. Its views point into the parsed bytes.
 */
struct http_request{
  std::string_view method;             /**< Method, e.g. "GET". */
  std::string_view target;             /**< Request target as sent, e.g. "/a%20b.crls". */
  std::array<http_header, http_max_headers> headers{}; /**< Storage of the headers, without allocations. */
  std::size_t header_count = 0;        /**< Number of headers. */

  /** @brief The headers received, in order. */
  std::span<const http_header> header_list() const noexcept{ return std::span(headers).first(header_count); }
  bool has_length = false;             /**< True if Content-Length was sent. */
  uint64_t content_length = 0;         /**< Value of Content-Length. */
  std::size_t head_length = 0;         /**< Bytes of the head, including the blank line. */
};

/** @brief A byte range, both ends included. */
struct http_range{
  uint64_t first = 0;                  /**< First byte. */
  uint64_t last = 0;                   /**< Last byte. */
};

/** @brief A parsed Content-Range header. */
struct http_content_range{
  uint64_t first = 0;                  /**< First byte. */
  uint64_t last = 0;                   /**< Last byte (inclusive). */
  uint64_t total = 0;                  /**< Size of the whole resource. */
};

/**
 * @brief  Parses the head of a request from the start of bytes.
 * @param  bytes Received bytes (the body may follow the head).
 * @param  req   Output: the request; valid only on http_parse::ok.
 * @return Parse result.
 */
http_parse http_parse_request(std::string_view bytes, http_request &req);

/**
 * @brief  Finds a header, matched case-insensitively.
 * @param  req  Parsed request.
 * @param  name Header name.
 * @return Its value, or std::nullopt if it was not sent.
 */
std::optional<std::string_view> http_header_value(const http_request &req, std::string_view name);

/**
 * @brief  Decodes the path of a request target ("/name%20x.crls" ->
 *         "name x.crls"). Queries, fragments, NUL bytes and malformed escapes
 *         are rejected.
 * @param  target Request target, starting with '/'.
 * @return The decoded path without the leading '/', or std::nullopt.
 */
std::optional<std::string> http_decode_path(std::string_view target);

/**
 * @brief  Parses a Range header against a resource of size bytes.
 * @param  value Header value, e.g. "bytes=0-99", "bytes=100-" or "bytes=-50".
 * @param  size  Size of the resource.
 * @return The range, or std::nullopt if it is malformed, multiple or
 *         unsatisfiable (416).
 */
std::optional<http_range> http_parse_range(std::string_view value, uint64_t size);

/**
 * @brief  Parses a Content-Range header of the form "bytes first-last/total".
 * @param  value Header value.
 * @return The range, or std::nullopt if it is malformed.
 */
std::optional<http_content_range> http_parse_content_range(std::string_view value);

/**
 * @brief  Parses a decimal unsigned 64-bit number, rejecting signs, spaces,
 *         empty strings and overflow.
 * @param  s Digits.
 * @return The number, or std::nullopt.
 */
std::optional<uint64_t> http_parse_u64(std::string_view s);

/**
 * @brief  Encodes bytes as lowercase hexadecimal.
 * @param  data Bytes.
 * @return Two characters per byte.
 */
std::string http_hex_encode(std::span<const unsigned char> data);

/**
 * @brief  Decodes hexadecimal into exactly out.size() bytes.
 * @param  hex Hexadecimal string of exactly 2 * out.size() characters.
 * @param  out Destination bytes.
 * @return True on success, false if hex has another length or invalid digits.
 */
bool http_hex_decode(std::string_view hex, std::span<unsigned char> out);

/**
 * @brief  Returns the reason phrase of a status code, e.g. "Not Found".
 * @param  status Status code.
 * @return Reason phrase.
 */
const char* http_reason(int status);

}

#endif
