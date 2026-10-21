/**
 * @file    test_httpmsg.cpp
 * @brief   Table-driven tests of the strict HTTP/1.1 parser (core/httpmsg.cpp).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include "core/httpmsg.h"
#include "test_util.h"

using namespace crls;

/** @brief One request head and the expected parse result. */
struct request_case{
  const char *text;                    /**< Raw request head. */
  http_parse expected;                 /**< Expected result. */
};

static const request_case requests[] = {
  {"GET /a.crls HTTP/1.1\r\nHost: x\r\n\r\n", http_parse::ok},
  {"HEAD / HTTP/1.0\r\n\r\n", http_parse::ok},
  {"POST /a.crls HTTP/1.1\r\nContent-Length: 10\r\n\r\n", http_parse::ok},
  {"GET /a.crls HTTP/1.1\r\nHost: x\r\n", http_parse::incomplete},
  {"GET /a.crls HTTP/1.1\r\nHost", http_parse::incomplete},
  {"GET /a.crls HTTP/2.0\r\n\r\n", http_parse::bad},
  {"get /a.crls HTTP/1.1\r\n\r\n", http_parse::bad},
  {"GET  HTTP/1.1\r\n\r\n", http_parse::bad},
  {"GET /a.crls\r\n\r\n", http_parse::bad},
  {"GET /a.crls HTTP/1.1\r\nNo colon here\r\n\r\n", http_parse::bad},
  {"GET /a.crls HTTP/1.1\r\n: empty name\r\n\r\n", http_parse::bad},
  {"GET /a.crls HTTP/1.1\r\n folded: header\r\n\r\n", http_parse::bad},
  {"GET /a.crls HTTP/1.1\r\nBad Name: x\r\n\r\n", http_parse::bad},
  {"POST /a.crls HTTP/1.1\r\nContent-Length: 10\r\nContent-Length: 10\r\n\r\n", http_parse::bad},
  {"POST /a.crls HTTP/1.1\r\nContent-Length: -1\r\n\r\n", http_parse::bad},
  {"POST /a.crls HTTP/1.1\r\nContent-Length: 1 0\r\n\r\n", http_parse::bad},
  {"POST /a.crls HTTP/1.1\r\nContent-Length: 99999999999999999999\r\n\r\n", http_parse::bad},
  {"POST /a.crls HTTP/1.1\r\nContent-Length: \r\n\r\n", http_parse::bad},
  {"POST /a.crls HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", http_parse::unsupported},
  {"POST /a.crls HTTP/1.1\r\ntransfer-encoding: chunked\r\nContent-Length: 5\r\n\r\n", http_parse::unsupported},
};

/** @brief A request target and the expected decoded path (NULL = rejected). */
struct path_case{
  const char *target;                  /**< Request target. */
  const char *expected;                /**< Decoded path, or NULL if rejected. */
};

static const path_case paths[] = {
  {"/a.crls", "a.crls"},
  {"/my%20files.crls", "my files.crls"},
  {"/", ""},
  {"/%2e%2e%2fsecret", "../secret"},       /* Decoded once; the server then rejects the name. */
  {"/a.crls?x=1", NULL},
  {"/a.crls#top", NULL},
  {"/a%00.crls", NULL},
  {"/a%zz.crls", NULL},
  {"/a%2", NULL},
  {"a.crls", NULL},
};

/** @brief A Range header and the expected range for a 1000-byte resource. */
struct range_case{
  const char *value;                   /**< Range header value. */
  bool valid;                          /**< True if the range is satisfiable. */
  uint64_t first, last;                /**< Expected range when valid. */
};

static const range_case ranges[] = {
  {"bytes=0-99", true, 0, 99},
  {"bytes=900-", true, 900, 999},
  {"bytes=-100", true, 900, 999},
  {"bytes=-5000", true, 0, 999},
  {"bytes=990-5000", true, 990, 999},
  {"bytes=1000-1001", false, 0, 0},
  {"bytes=5-4", false, 0, 0},
  {"bytes=0-1,5-6", false, 0, 0},
  {"bytes=-0", false, 0, 0},
  {"bytes=a-b", false, 0, 0},
  {"items=0-1", false, 0, 0},
};

int main(){
  for(size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++){
    http_request req;
    http_parse r = http_parse_request(requests[i].text, req);
    if(r != requests[i].expected){
      printf("FAIL request %zu: got %d, expected %d\n", i, static_cast<int>(r), static_cast<int>(requests[i].expected));
      failures++;
    }
  }

  /* Header lookup, value trimming and the body offset. */
  std::string_view text = "POST /x.crls HTTP/1.1\r\nX-Crls-Sig:   abc  \r\nContent-Length: 4\r\n\r\nBODY";
  http_request req;
  CHECK(http_parse_request(text, req) == http_parse::ok);
  CHECK(req.method == "POST" && req.target == "/x.crls");
  CHECK(http_header_value(req, "x-crls-sig") == std::optional<std::string_view>("abc"));
  CHECK(req.has_length && req.content_length == 4);
  CHECK(text.substr(req.head_length) == "BODY");
  CHECK(!http_header_value(req, "Missing"));

  /* Limits on line length and header count. */
  std::string big = "GET /" + std::string(http_max_line, 'a');
  CHECK(http_parse_request(big, req) == http_parse::too_large);
  big = "GET / HTTP/1.1\r\n";
  for(size_t i = 0; i <= http_max_headers; i++) big += "H" + std::to_string(i) + ": v\r\n";
  big += "\r\n";
  CHECK(http_parse_request(big, req) == http_parse::too_large);

  for(size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++){
    std::string out;
    bool ok = http_decode_path(paths[i].target, out);
    if(ok != (paths[i].expected != NULL) || (ok && out != paths[i].expected)){
      printf("FAIL path %s\n", paths[i].target);
      failures++;
    }
  }

  for(size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++){
    std::optional<http_range> r = http_parse_range(ranges[i].value, 1000);
    if(r.has_value() != ranges[i].valid || (r && (r->first != ranges[i].first || r->last != ranges[i].last))){
      printf("FAIL range %s\n", ranges[i].value);
      failures++;
    }
  }

  std::optional<http_content_range> range = http_parse_content_range("bytes 0-99/1000");
  CHECK(range && range->first == 0 && range->last == 99 && range->total == 1000);
  CHECK(!http_parse_content_range("bytes 0-1000/1000"));
  CHECK(!http_parse_content_range("bytes */1000"));

  const unsigned char bytes[3] = {0x00, 0xab, 0xff};
  unsigned char back[3];
  CHECK(http_hex_encode(bytes) == "00abff");
  CHECK(http_hex_decode("00ABff", back) && memcmp(back, bytes, 3) == 0);
  CHECK(!http_hex_decode("00abf", back));
  CHECK(!http_hex_decode("00abfg", back));

  CHECK(http_parse_u64("18446744073709551615") == UINT64_MAX);
  CHECK(!http_parse_u64("18446744073709551616"));
  CHECK(!http_parse_u64("") && !http_parse_u64("+1") && !http_parse_u64(" 1"));

  return test_summary();
}
