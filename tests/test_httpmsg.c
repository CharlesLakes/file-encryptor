/**
 * @file    test_httpmsg.c
 * @brief   Table-driven tests of the strict HTTP/1.1 parser (core/httpmsg.c).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include <stdio.h>
#include <string.h>
#include "core/httpmsg.h"
#include "test_util.h"

/** @brief One request head and the expected parse result. */
typedef struct{
  const char *text;                    /**< Raw request head. */
  http_parse_result expected;          /**< Expected result. */
}request_case;

static const request_case requests[] = {
  {"GET /a.crls HTTP/1.1\r\nHost: x\r\n\r\n", HTTP_PARSE_OK},
  {"HEAD / HTTP/1.0\r\n\r\n", HTTP_PARSE_OK},
  {"POST /a.crls HTTP/1.1\r\nContent-Length: 10\r\n\r\n", HTTP_PARSE_OK},
  {"GET /a.crls HTTP/1.1\r\nHost: x\r\n", HTTP_PARSE_INCOMPLETE},
  {"GET /a.crls HTTP/1.1\r\nHost", HTTP_PARSE_INCOMPLETE},
  {"GET /a.crls HTTP/2.0\r\n\r\n", HTTP_PARSE_BAD},
  {"get /a.crls HTTP/1.1\r\n\r\n", HTTP_PARSE_BAD},
  {"GET  HTTP/1.1\r\n\r\n", HTTP_PARSE_BAD},
  {"GET /a.crls\r\n\r\n", HTTP_PARSE_BAD},
  {"GET /a.crls HTTP/1.1\r\nNo colon here\r\n\r\n", HTTP_PARSE_BAD},
  {"GET /a.crls HTTP/1.1\r\n: empty name\r\n\r\n", HTTP_PARSE_BAD},
  {"GET /a.crls HTTP/1.1\r\n folded: header\r\n\r\n", HTTP_PARSE_BAD},
  {"GET /a.crls HTTP/1.1\r\nBad Name: x\r\n\r\n", HTTP_PARSE_BAD},
  {"POST /a.crls HTTP/1.1\r\nContent-Length: 10\r\nContent-Length: 10\r\n\r\n", HTTP_PARSE_BAD},
  {"POST /a.crls HTTP/1.1\r\nContent-Length: -1\r\n\r\n", HTTP_PARSE_BAD},
  {"POST /a.crls HTTP/1.1\r\nContent-Length: 1 0\r\n\r\n", HTTP_PARSE_BAD},
  {"POST /a.crls HTTP/1.1\r\nContent-Length: 99999999999999999999\r\n\r\n", HTTP_PARSE_BAD},
  {"POST /a.crls HTTP/1.1\r\nContent-Length: \r\n\r\n", HTTP_PARSE_BAD},
  {"POST /a.crls HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", HTTP_PARSE_UNSUPPORTED},
  {"POST /a.crls HTTP/1.1\r\ntransfer-encoding: chunked\r\nContent-Length: 5\r\n\r\n", HTTP_PARSE_UNSUPPORTED},
};

/** @brief A request target and the expected decoded path (NULL = rejected). */
typedef struct{
  const char *target;                  /**< Request target. */
  const char *expected;                /**< Decoded path, or NULL if rejected. */
}path_case;

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
typedef struct{
  const char *value;                   /**< Range header value. */
  http_range_result expected;          /**< Expected result. */
  uint64_t first, last;                /**< Expected range when valid. */
}range_case;

static const range_case ranges[] = {
  {"bytes=0-99", HTTP_RANGE_OK, 0, 99},
  {"bytes=900-", HTTP_RANGE_OK, 900, 999},
  {"bytes=-100", HTTP_RANGE_OK, 900, 999},
  {"bytes=-5000", HTTP_RANGE_OK, 0, 999},
  {"bytes=990-5000", HTTP_RANGE_OK, 990, 999},
  {"bytes=1000-1001", HTTP_RANGE_INVALID, 0, 0},
  {"bytes=5-4", HTTP_RANGE_INVALID, 0, 0},
  {"bytes=0-1,5-6", HTTP_RANGE_INVALID, 0, 0},
  {"bytes=-0", HTTP_RANGE_INVALID, 0, 0},
  {"bytes=a-b", HTTP_RANGE_INVALID, 0, 0},
  {"items=0-1", HTTP_RANGE_INVALID, 0, 0},
};

int main(void){
  for(size_t i = 0; i < sizeof(requests) / sizeof(requests[0]); i++){
    http_request req;
    http_parse_result r = http_parse_request(requests[i].text, strlen(requests[i].text), &req);
    if(r != requests[i].expected){
      printf("FAIL request %zu: got %d, expected %d\n", i, (int) r, (int) requests[i].expected);
      failures++;
    }
  }

  /* Header lookup, value trimming and the body offset. */
  const char *text = "POST /x.crls HTTP/1.1\r\nX-Crls-Sig:   abc  \r\nContent-Length: 4\r\n\r\nBODY";
  http_request req;
  char value[64];
  CHECK(http_parse_request(text, strlen(text), &req) == HTTP_PARSE_OK);
  CHECK(strcmp(req.method, "POST") == 0 && strcmp(req.target, "/x.crls") == 0);
  CHECK(http_get_header(&req, "x-crls-sig", value, sizeof(value)) && strcmp(value, "abc") == 0);
  CHECK(req.has_length && req.content_length == 4);
  CHECK(strcmp(text + req.head_length, "BODY") == 0);
  CHECK(!http_get_header(&req, "Missing", value, sizeof(value)));

  /* Limits on line length and header count. */
  static char big[HTTP_MAX_HEAD + 64];
  snprintf(big, sizeof(big), "GET /");
  memset(big + 5, 'a', HTTP_MAX_LINE);
  big[5 + HTTP_MAX_LINE] = '\0';
  CHECK(http_parse_request(big, strlen(big), &req) == HTTP_PARSE_TOO_LARGE);
  size_t n = (size_t) snprintf(big, sizeof(big), "GET / HTTP/1.1\r\n");
  for(int i = 0; i <= HTTP_MAX_HEADERS; i++) n += (size_t) snprintf(big + n, sizeof(big) - n, "H%d: v\r\n", i);
  snprintf(big + n, sizeof(big) - n, "\r\n");
  CHECK(http_parse_request(big, strlen(big), &req) == HTTP_PARSE_TOO_LARGE);

  for(size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++){
    char out[256];
    bool ok = http_decode_path(paths[i].target, out, sizeof(out));
    if(ok != (paths[i].expected != NULL) || (ok && strcmp(out, paths[i].expected) != 0)){
      printf("FAIL path %s\n", paths[i].target);
      failures++;
    }
  }

  for(size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++){
    uint64_t first = 0, last = 0;
    http_range_result r = http_parse_range(ranges[i].value, 1000, &first, &last);
    if(r != ranges[i].expected || (r == HTTP_RANGE_OK && (first != ranges[i].first || last != ranges[i].last))){
      printf("FAIL range %s\n", ranges[i].value);
      failures++;
    }
  }

  uint64_t first, last, total;
  CHECK(http_parse_content_range("bytes 0-99/1000", &first, &last, &total) && first == 0 && last == 99 && total == 1000);
  CHECK(!http_parse_content_range("bytes 0-1000/1000", &first, &last, &total));
  CHECK(!http_parse_content_range("bytes */1000", &first, &last, &total));

  unsigned char bytes[3] = {0x00, 0xab, 0xff}, back[3];
  char hex[7];
  http_hex_encode(bytes, 3, hex);
  CHECK(strcmp(hex, "00abff") == 0);
  CHECK(http_hex_decode("00ABff", back, 3) && memcmp(back, bytes, 3) == 0);
  CHECK(!http_hex_decode("00abf", back, 3));
  CHECK(!http_hex_decode("00abfg", back, 3));

  return test_summary();
}
