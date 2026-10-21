/**
 * @file    http_winhttp.cpp
 * @brief   WinHTTP implementation of the HTTP abstraction (see http.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * WinHTTP is part of Windows and does its own TLS with the system
 * certificates. WinINet is avoided on purpose: it caches responses, which
 * does not mix well with byte ranges.
 */

#include "core/http.h"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <windows.h>
#include <winhttp.h>
#include "core/fs.h"

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace crls{

namespace{

/* Scratch buffer for request and response bodies, one request at a time.
   Static because it is too large for the stack of a Windows thread. */
std::array<unsigned char, 64 * 1024> transfer_chunk;

/** @brief Deleter of a WinHTTP handle. */
struct handle_deleter{
  void operator()(void *h) const noexcept{ WinHttpCloseHandle(h); }
};

using internet_handle = std::unique_ptr<void, handle_deleter>;

/**
 * @brief  Converts a UTF-8 string to UTF-16.
 * @param  s UTF-8 string.
 * @return The converted string (empty if s is not valid UTF-8).
 */
std::wstring to_wide(std::string_view s){
  if(s.empty()) return std::wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(n > 0 ? static_cast<size_t>(n) : 0, L'\0');
  if(n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

/**
 * @brief  Converts a UTF-16 string to UTF-8.
 * @param  s UTF-16 string.
 * @return The converted string (empty if s cannot be converted).
 */
std::string to_utf8(std::wstring_view s){
  if(s.empty()) return std::string();
  int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
  std::string out(n > 0 ? static_cast<size_t>(n) : 0, '\0');
  if(n > 0) WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
  return out;
}

/**
 * @brief  Returns a response header as UTF-8, whatever its length.
 * @param  request Request handle.
 * @param  name    Header name.
 * @return The value, or an empty string if the header is missing.
 */
std::string query_header(HINTERNET request, const wchar_t *name){
  /* The first call only reports the size, in bytes. */
  DWORD size = 0;
  WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
  if(GetLastError() != ERROR_INSUFFICIENT_BUFFER || size == 0) return std::string();
  std::wstring value(size / sizeof(wchar_t), L'\0');
  if(!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, value.data(), &size, WINHTTP_NO_HEADER_INDEX))
    return std::string();
  value.resize(size / sizeof(wchar_t));
  return to_utf8(value);
}

/**
 * @brief  Records the last Windows error as the transport error.
 * @param  resp Response.
 * @param  what Step that failed.
 * @return False, for convenience.
 */
bool fail(http_response &resp, const char *what){
  resp.error = std::string(what) + " failed (error " + std::to_string(GetLastError()) + ").";
  return false;
}

}

bool http_perform(const http_request_spec &req, http_response &resp){
  resp = http_response();
  std::wstring url = to_wide(req.url), method = to_wide(req.method);
  std::array<wchar_t, 256> host;
  std::array<wchar_t, 4096> path;
  if(url.empty() || method.empty()){
    resp.error = "Invalid URL.";
    return false;
  }

  URL_COMPONENTS parts{};
  parts.dwStructSize = sizeof(parts);
  parts.lpszHostName = host.data();
  parts.dwHostNameLength = static_cast<DWORD>(host.size());
  parts.lpszUrlPath = path.data();
  parts.dwUrlPathLength = static_cast<DWORD>(path.size());
  if(!WinHttpCrackUrl(url.c_str(), 0, 0, &parts) ||
     (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS)){
    resp.error = "Only http and https URLs are supported.";
    return false;
  }

  internet_handle session(WinHttpOpen(L"FileEncryptor", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  /* Automatic proxy detection needs Windows 8.1; older versions use the default. */
  if(!session) session.reset(WinHttpOpen(L"FileEncryptor", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                         WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
  if(!session) return fail(resp, "WinHttpOpen");
  internet_handle connection(WinHttpConnect(session.get(), host.data(), parts.nPort, 0));
  if(!connection) return fail(resp, "WinHttpConnect");
  internet_handle request(WinHttpOpenRequest(connection.get(), method.c_str(), path.data(), nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
  if(!request) return fail(resp, "WinHttpOpenRequest");

  DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
  WinHttpSetOption(request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never));
  WinHttpSetTimeouts(request.get(), 15000, 15000, 30000, 30000);
  for(const std::string &line : req.headers){
    std::wstring header = to_wide(line);
    if(!WinHttpAddRequestHeaders(request.get(), header.c_str(), static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD))
      return fail(resp, "WinHttpAddRequestHeaders");
  }

  /* The total length is a DWORD: larger bodies send Content-Length themselves. */
  DWORD total = static_cast<DWORD>(req.body_len);
  if(req.body_len > 0xFFFFFFFFull){
    std::wstring header = L"Content-Length: " + std::to_wstring(req.body_len);
    WinHttpAddRequestHeaders(request.get(), header.c_str(), static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD);
    total = WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH;
  }
  if(!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, total, 0))
    return fail(resp, "WinHttpSendRequest");
  for(uint64_t sent = 0; sent < req.body_len; ){
    std::span<unsigned char> chunk = std::span(transfer_chunk)
                                         .first(static_cast<size_t>(std::min<uint64_t>(req.body_len - sent, transfer_chunk.size())));
    DWORD n = static_cast<DWORD>(chunk.size()), written = 0;
    if(!read_at(req.body_fd, chunk, sent) || !WinHttpWriteData(request.get(), chunk.data(), n, &written) ||
       written != n) return fail(resp, "WinHttpWriteData");
    sent += n;
  }
  if(!WinHttpReceiveResponse(request.get(), nullptr)) return fail(resp, "WinHttpReceiveResponse");

  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &status, &size, WINHTTP_NO_HEADER_INDEX);
  resp.status = static_cast<int>(status);
  resp.content_range = query_header(request.get(), L"Content-Range");
  resp.etag = query_header(request.get(), L"ETag");

  while(req.method != "HEAD"){
    DWORD n = 0;
    if(!WinHttpReadData(request.get(), transfer_chunk.data(), static_cast<DWORD>(transfer_chunk.size()), &n))
      return fail(resp, "WinHttpReadData");
    if(n == 0) break;
    if(req.sink && !req.sink(std::span(transfer_chunk).first(n))){
      resp.error = "The response was rejected.";
      return false;
    }
  }
  return true;
}

}
