/**
 * @file    httpmsg.cpp
 * @brief   Implementation of the HTTP/1.1 helpers (see httpmsg.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/httpmsg.h"

#include "core/util.h"

namespace crls{

namespace{

/**
 * @brief  Checks whether c may appear in a header name (RFC 9110 token).
 * @param  c Character.
 * @return True if c is a token character.
 */
bool is_token_char(char c){
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
         std::string_view("!#$%&'*+-.^_`|~").find(c) != std::string_view::npos;
}

/**
 * @brief  Returns the value of a hexadecimal digit.
 * @param  c Character.
 * @return 0 to 15, or -1 if c is not a hexadecimal digit.
 */
int hex_value(char c){
  if(c >= '0' && c <= '9') return c - '0';
  if(c >= 'a' && c <= 'f') return c - 'a' + 10;
  if(c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/**
 * @brief  Parses the request line "METHOD SP target SP HTTP/1.x".
 * @param  line Request line without CRLF.
 * @param  req  Request whose method and target are set.
 * @return True if the line is valid.
 */
bool parse_request_line(std::string_view line, http_request &req){
  std::size_t sp1 = line.find(' ');
  std::size_t sp2 = sp1 == std::string_view::npos ? sp1 : line.find(' ', sp1 + 1);
  if(sp2 == std::string_view::npos) return false;
  std::string_view method = line.substr(0, sp1), target = line.substr(sp1 + 1, sp2 - sp1 - 1);
  std::string_view version = line.substr(sp2 + 1);
  if(method.empty() || method.size() >= 16 || target.empty() || target.size() >= 1024) return false;
  if(version != "HTTP/1.1" && version != "HTTP/1.0") return false;
  for(char c : method)
    if(c < 'A' || c > 'Z') return false;
  req.method = method;
  req.target = target;
  return true;
}

}

std::optional<uint64_t> http_parse_u64(std::string_view s){
  if(s.empty() || s.size() > 20) return std::nullopt;
  uint64_t v = 0;
  for(char c : s){
    if(c < '0' || c > '9') return std::nullopt;
    uint64_t digit = static_cast<uint64_t>(c - '0');
    if(v > (UINT64_MAX - digit) / 10) return std::nullopt;
    v = v * 10 + digit;
  }
  return v;
}

http_parse http_parse_request(std::string_view bytes, http_request &req){
  req = http_request();
  std::size_t pos = 0;
  bool first_line = true;

  while(true){
    std::size_t eol = bytes.find("\r\n", pos);
    if(eol == std::string_view::npos){
      if(bytes.size() - pos >= http_max_line || bytes.size() >= http_max_head) return http_parse::too_large;
      return http_parse::incomplete;
    }
    std::string_view line = bytes.substr(pos, eol - pos);
    if(line.size() >= http_max_line || eol + 2 > http_max_head) return http_parse::too_large;
    if(line.find_first_of(std::string_view("\0\r\n", 3)) != std::string_view::npos) return http_parse::bad;

    if(first_line){
      if(!parse_request_line(line, req)) return http_parse::bad;
      first_line = false;
    }else if(line.empty()){
      req.head_length = eol + 2;
      return http_parse::ok;
    }else{
      std::size_t colon = line.find(':');
      if(colon == std::string_view::npos || colon == 0) return http_parse::bad;
      std::string_view name = line.substr(0, colon);
      for(char c : name)
        if(!is_token_char(c)) return http_parse::bad;
      if(req.header_count >= http_max_headers) return http_parse::too_large;

      http_header &h = req.headers[req.header_count++];
      h.name = name;
      h.value = trim(line.substr(colon + 1));

      if(equals_nocase(h.name, "Transfer-Encoding")) return http_parse::unsupported;
      if(equals_nocase(h.name, "Content-Length")){
        std::optional<uint64_t> length = http_parse_u64(h.value);
        if(req.has_length || !length) return http_parse::bad;
        req.content_length = *length;
        req.has_length = true;
      }
    }
    pos = eol + 2;
  }
}

std::optional<std::string_view> http_header_value(const http_request &req, std::string_view name){
  for(std::size_t i = 0; i < req.header_count; i++)
    if(equals_nocase(req.headers[i].name, name)) return req.headers[i].value;
  return std::nullopt;
}

bool http_decode_path(std::string_view target, std::string &out){
  out.clear();
  if(target.empty() || target[0] != '/') return false;
  for(std::size_t i = 1; i < target.size(); i++){
    char c = target[i];
    if(c == '?' || c == '#') return false;
    if(c == '%'){
      int hi = i + 1 < target.size() ? hex_value(target[i + 1]) : -1;
      int lo = hi >= 0 && i + 2 < target.size() ? hex_value(target[i + 2]) : -1;
      if(hi < 0 || lo < 0) return false;
      c = static_cast<char>(hi * 16 + lo);
      if(c == '\0') return false;
      i += 2;
    }
    out += c;
  }
  return true;
}

std::optional<http_range> http_parse_range(std::string_view value, uint64_t size){
  if(!value.starts_with("bytes=")) return std::nullopt;
  std::string_view spec = value.substr(6);
  std::size_t dash = spec.find('-');
  if(dash == std::string_view::npos || spec.find(',') != std::string_view::npos) return std::nullopt;
  std::string_view a = spec.substr(0, dash), b = spec.substr(dash + 1);

  if(a.empty()){
    /* Suffix range: the last b bytes. */
    std::optional<uint64_t> count = http_parse_u64(b);
    if(!count || *count == 0 || size == 0) return std::nullopt;
    return http_range{*count >= size ? 0 : size - *count, size - 1};
  }
  std::optional<uint64_t> first = http_parse_u64(a);
  if(!first || *first >= size) return std::nullopt;
  uint64_t last = size - 1;
  if(!b.empty()){
    std::optional<uint64_t> end = http_parse_u64(b);
    if(!end || *end < *first) return std::nullopt;
    if(*end < last) last = *end;
  }
  return http_range{*first, last};
}

std::optional<http_content_range> http_parse_content_range(std::string_view value){
  if(!value.starts_with("bytes ")) return std::nullopt;
  std::string_view spec = value.substr(6);
  std::size_t dash = spec.find('-'), slash = spec.find('/');
  if(dash == std::string_view::npos || slash == std::string_view::npos || slash < dash) return std::nullopt;
  std::optional<uint64_t> first = http_parse_u64(spec.substr(0, dash));
  std::optional<uint64_t> last = http_parse_u64(spec.substr(dash + 1, slash - dash - 1));
  std::optional<uint64_t> total = http_parse_u64(spec.substr(slash + 1));
  if(!first || !last || !total || *first > *last || *last >= *total) return std::nullopt;
  return http_content_range{*first, *last, *total};
}

std::string http_hex_encode(std::span<const unsigned char> data){
  static const char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(2 * data.size());
  for(unsigned char b : data){
    out += digits[b >> 4];
    out += digits[b & 15];
  }
  return out;
}

bool http_hex_decode(std::string_view hex, std::span<unsigned char> out){
  if(hex.size() != 2 * out.size()) return false;
  for(std::size_t i = 0; i < out.size(); i++){
    int hi = hex_value(hex[2*i]), lo = hex_value(hex[2*i + 1]);
    if(hi < 0 || lo < 0) return false;
    out[i] = static_cast<unsigned char>(hi * 16 + lo);
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

}
