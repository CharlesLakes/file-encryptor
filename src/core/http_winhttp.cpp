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

#include <cstdio>
#include <cstring>
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

/* Scratch buffer for request and response bodies, one request at a time. */
unsigned char transfer_chunk[64 * 1024];

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
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), NULL, 0);
  std::wstring out(n > 0 ? static_cast<size_t>(n) : 0, L'\0');
  if(n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

/**
 * @brief Copies a response header as UTF-8.
 * @param request Request handle.
 * @param name    Header name.
 * @param out     Output: the value, or empty.
 */
void query_header(HINTERNET request, const wchar_t *name, std::string &out){
  wchar_t value[256];
  DWORD size = sizeof(value);
  out.clear();
  if(!WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, value, &size, WINHTTP_NO_HEADER_INDEX)) return;
  char utf8[512];
  if(WideCharToMultiByte(CP_UTF8, 0, value, -1, utf8, sizeof(utf8), NULL, NULL) > 0) out = utf8;
}

/**
 * @brief  Records the last Windows error as the transport error.
 * @param  resp Response.
 * @param  what Step that failed.
 * @return False, for convenience.
 */
bool fail(http_response &resp, const char *what){
  char text[128];
  snprintf(text, sizeof(text), "%s failed (error %lu).", what, static_cast<unsigned long>(GetLastError()));
  resp.error = text;
  return false;
}

}

bool http_perform(const http_request_spec &req, http_response &resp){
  resp = http_response();
  std::wstring url = to_wide(req.url), method = to_wide(req.method);
  wchar_t host[256], path[4096];
  if(url.empty() || method.empty()){
    resp.error = "Invalid URL.";
    return false;
  }

  URL_COMPONENTS parts;
  memset(&parts, 0, sizeof(parts));
  parts.dwStructSize = sizeof(parts);
  parts.lpszHostName = host;
  parts.dwHostNameLength = 256;
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = 4096;
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
  internet_handle connection(WinHttpConnect(session.get(), host, parts.nPort, 0));
  if(!connection) return fail(resp, "WinHttpConnect");
  internet_handle request(WinHttpOpenRequest(connection.get(), method.c_str(), path, NULL, WINHTTP_NO_REFERER,
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
    DWORD n = req.body_len - sent < sizeof(transfer_chunk) ? static_cast<DWORD>(req.body_len - sent) : sizeof(transfer_chunk);
    DWORD written = 0;
    if(!read_at(req.body_fd, transfer_chunk, n, sent) || !WinHttpWriteData(request.get(), transfer_chunk, n, &written) ||
       written != n) return fail(resp, "WinHttpWriteData");
    sent += n;
  }
  if(!WinHttpReceiveResponse(request.get(), NULL)) return fail(resp, "WinHttpReceiveResponse");

  DWORD status = 0, size = sizeof(status);
  WinHttpQueryHeaders(request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &status, &size, WINHTTP_NO_HEADER_INDEX);
  resp.status = static_cast<int>(status);
  query_header(request.get(), L"Content-Range", resp.content_range);
  query_header(request.get(), L"ETag", resp.etag);

  while(req.method != "HEAD"){
    DWORD n = 0;
    if(!WinHttpReadData(request.get(), transfer_chunk, sizeof(transfer_chunk), &n)) return fail(resp, "WinHttpReadData");
    if(n == 0) break;
    if(req.sink && !req.sink({transfer_chunk, n})){
      resp.error = "The response was rejected.";
      return false;
    }
  }
  return true;
}

}
