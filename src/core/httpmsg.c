/**
 * @file    httpmsg.c
 * @brief   Implementation of the HTTP/1.1 helpers (see httpmsg.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/httpmsg.h"

#include <string.h>

/**
 * @brief  Compares two strings of known length case-insensitively (ASCII).
 * @param  a     First string.
 * @param  a_len Length of a.
 * @param  b     Second string, NUL-terminated.
 * @return True if they are equal.
 */
static bool equals_nocase(const char *a, size_t a_len, const char *b){
  if(a_len != strlen(b)) return false;
  for(size_t i = 0; i < a_len; i++){
    char x = a[i], y = b[i];
    if(x >= 'A' && x <= 'Z') x = (char) (x - 'A' + 'a');
    if(y >= 'A' && y <= 'Z') y = (char) (y - 'A' + 'a');
    if(x != y) return false;
  }
  return true;
}

/**
 * @brief  Checks whether c may appear in a header name (RFC 9110 token).
 * @param  c Character.
 * @return True if c is a token character.
 */
static bool is_token_char(char c){
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         (c != 0 && strchr("!#$%&'*+-.^_`|~", c) != NULL);
}

bool http_parse_u64(const char *s, size_t len, uint64_t *value){
  if(len == 0 || len > 20) return false;
  uint64_t v = 0;
  for(size_t i = 0; i < len; i++){
    if(s[i] < '0' || s[i] > '9') return false;
    uint64_t digit = (uint64_t) (s[i] - '0');
    if(v > (UINT64_MAX - digit) / 10) return false;
    v = v * 10 + digit;
  }
  *value = v;
  return true;
}

http_parse_result http_parse_request(const char *buf, size_t len, http_request *req){
  memset(req, 0, sizeof(*req));
  const char *end = buf + len, *p = buf;
  bool first_line = true;

  while(true){
    const char *eol = NULL;
    for(const char *q = p; q + 1 < end; q++)
      if(q[0] == '\r' && q[1] == '\n'){
        eol = q;
        break;
      }
    if(!eol){
      if((size_t) (end - p) >= HTTP_MAX_LINE || len >= HTTP_MAX_HEAD) return HTTP_PARSE_TOO_LARGE;
      return HTTP_PARSE_INCOMPLETE;
    }
    size_t line_len = (size_t) (eol - p);
    if(line_len >= HTTP_MAX_LINE || (size_t) (eol + 2 - buf) > HTTP_MAX_HEAD) return HTTP_PARSE_TOO_LARGE;
    for(size_t i = 0; i < line_len; i++)
      if(p[i] == '\0' || p[i] == '\r' || p[i] == '\n') return HTTP_PARSE_BAD;

    if(first_line){
      /* METHOD SP target SP HTTP/1.x */
      const char *sp1 = (const char *) memchr(p, ' ', line_len);
      const char *sp2 = sp1 ? (const char *) memchr(sp1 + 1, ' ', (size_t) (eol - sp1 - 1)) : NULL;
      if(!sp1 || !sp2) return HTTP_PARSE_BAD;
      size_t method_len = (size_t) (sp1 - p), target_len = (size_t) (sp2 - sp1 - 1);
      size_t version_len = (size_t) (eol - sp2 - 1);
      if(method_len == 0 || method_len >= sizeof(req->method) || target_len == 0 ||
         target_len >= sizeof(req->target)) return HTTP_PARSE_BAD;
      if(version_len != 8 || (memcmp(sp2 + 1, "HTTP/1.1", 8) != 0 && memcmp(sp2 + 1, "HTTP/1.0", 8) != 0))
        return HTTP_PARSE_BAD;
      for(size_t i = 0; i < method_len; i++)
        if(p[i] < 'A' || p[i] > 'Z') return HTTP_PARSE_BAD;
      memcpy(req->method, p, method_len);
      memcpy(req->target, sp1 + 1, target_len);
      first_line = false;
    }else if(line_len == 0){
      req->head_length = (size_t) (eol + 2 - buf);
      return HTTP_PARSE_OK;
    }else{
      const char *colon = (const char *) memchr(p, ':', line_len);
      if(!colon || colon == p) return HTTP_PARSE_BAD;
      for(const char *q = p; q < colon; q++)
        if(!is_token_char(*q)) return HTTP_PARSE_BAD;
      if(req->header_count >= HTTP_MAX_HEADERS) return HTTP_PARSE_TOO_LARGE;

      const char *value = colon + 1, *value_end = eol;
      while(value < value_end && (*value == ' ' || *value == '\t')) value++;
      while(value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t')) value_end--;
      http_header *h = &req->headers[req->header_count++];
      h->name = p;
      h->name_len = (size_t) (colon - p);
      h->value = value;
      h->value_len = (size_t) (value_end - value);

      if(equals_nocase(h->name, h->name_len, "Transfer-Encoding")) return HTTP_PARSE_UNSUPPORTED;
      if(equals_nocase(h->name, h->name_len, "Content-Length")){
        if(req->has_length || !http_parse_u64(h->value, h->value_len, &req->content_length)) return HTTP_PARSE_BAD;
        req->has_length = true;
      }
    }
    p = eol + 2;
  }
}

bool http_get_header(const http_request *req, const char *name, char *out, size_t out_size){
  for(int i = 0; i < req->header_count; i++){
    const http_header *h = &req->headers[i];
    if(!equals_nocase(h->name, h->name_len, name)) continue;
    if(h->value_len >= out_size) return false;
    memcpy(out, h->value, h->value_len);
    out[h->value_len] = '\0';
    return true;
  }
  return false;
}

/**
 * @brief  Returns the value of a hexadecimal digit.
 * @param  c Character.
 * @return 0 to 15, or -1 if c is not a hexadecimal digit.
 */
static int hex_value(char c){
  if(c >= '0' && c <= '9') return c - '0';
  if(c >= 'a' && c <= 'f') return c - 'a' + 10;
  if(c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool http_decode_path(const char *target, char *out, size_t out_size){
  if(target[0] != '/') return false;
  size_t n = 0;
  for(const char *p = target + 1; *p; p++){
    char c = *p;
    if(c == '?' || c == '#') return false;
    if(c == '%'){
      int hi = hex_value(p[1]), lo = hi < 0 ? -1 : hex_value(p[2]);
      if(hi < 0 || lo < 0) return false;
      c = (char) (hi * 16 + lo);
      if(c == '\0') return false;
      p += 2;
    }
    if(n + 1 >= out_size) return false;
    out[n++] = c;
  }
  out[n] = '\0';
  return true;
}

http_range_result http_parse_range(const char *value, uint64_t size, uint64_t *first, uint64_t *last){
  if(strncmp(value, "bytes=", 6) != 0) return HTTP_RANGE_INVALID;
  const char *spec = value + 6, *dash = strchr(spec, '-');
  if(!dash || strchr(spec, ',')) return HTTP_RANGE_INVALID;
  size_t a_len = (size_t) (dash - spec), b_len = strlen(dash + 1);
  uint64_t a, b;

  if(a_len == 0){
    /* Suffix range: the last b bytes. */
    if(!http_parse_u64(dash + 1, b_len, &b) || b == 0 || size == 0) return HTTP_RANGE_INVALID;
    *first = b >= size ? 0 : size - b;
    *last = size - 1;
    return HTTP_RANGE_OK;
  }
  if(!http_parse_u64(spec, a_len, &a) || a >= size) return HTTP_RANGE_INVALID;
  if(b_len == 0) b = size - 1;
  else if(!http_parse_u64(dash + 1, b_len, &b) || b < a) return HTTP_RANGE_INVALID;
  *first = a;
  *last = b >= size ? size - 1 : b;
  return HTTP_RANGE_OK;
}

bool http_parse_content_range(const char *value, uint64_t *first, uint64_t *last, uint64_t *total){
  if(strncmp(value, "bytes ", 6) != 0) return false;
  const char *spec = value + 6, *dash = strchr(spec, '-'), *slash = strchr(spec, '/');
  if(!dash || !slash || slash < dash) return false;
  return http_parse_u64(spec, (size_t) (dash - spec), first) &&
         http_parse_u64(dash + 1, (size_t) (slash - dash - 1), last) &&
         http_parse_u64(slash + 1, strlen(slash + 1), total) &&
         *first <= *last && *last < *total;
}

void http_hex_encode(const unsigned char *data, size_t len, char *out){
  static const char digits[] = "0123456789abcdef";
  for(size_t i = 0; i < len; i++){
    out[2*i] = digits[data[i] >> 4];
    out[2*i + 1] = digits[data[i] & 15];
  }
  out[2*len] = '\0';
}

bool http_hex_decode(const char *hex, unsigned char *out, size_t len){
  if(strlen(hex) != 2 * len) return false;
  for(size_t i = 0; i < len; i++){
    int hi = hex_value(hex[2*i]), lo = hex_value(hex[2*i + 1]);
    if(hi < 0 || lo < 0) return false;
    out[i] = (unsigned char) (hi * 16 + lo);
  }
  return true;
}

const char* http_reason(int status){
  switch(status){
    case 100: return "Continue";
    case 200: return "OK";
    case 204: return "No Content";
    case 206: return "Partial Content";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Content Too Large";
    case 416: return "Range Not Satisfiable";
    case 422: return "Unprocessable Content";
    case 423: return "Locked";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 503: return "Service Unavailable";
  }
  return "Unknown";
}
