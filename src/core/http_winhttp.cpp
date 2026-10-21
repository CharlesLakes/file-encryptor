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

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <winhttp.h>
#include "core/fs.h"

namespace crls{

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

/** @brief Scratch buffer for request and response bodies. */
static unsigned char transfer_chunk[64 * 1024];

/**
 * @brief  Converts a UTF-8 string to UTF-16.
 * @param  s   UTF-8 string.
 * @param  out Destination buffer.
 * @param  len Capacity of out in characters.
 * @return True on success.
 */
static bool to_wide(const char *s, wchar_t *out, int len){
  return MultiByteToWideChar(CP_UTF8, 0, s, -1, out, len) > 0;
}

/**
 * @brief  Copies a response header as UTF-8.
 * @param  request  Request handle.
 * @param  name     Header name.
 * @param  out      Destination buffer.
 * @param  out_size Capacity of out.
 */
static void query_header(HINTERNET request, const wchar_t *name, char *out, size_t out_size){
  wchar_t value[256];
  DWORD size = sizeof(value);
  out[0] = '\0';
  if(WinHttpQueryHeaders(request, WINHTTP_QUERY_CUSTOM, name, value, &size, WINHTTP_NO_HEADER_INDEX))
    WideCharToMultiByte(CP_UTF8, 0, value, -1, out, (int) out_size, NULL, NULL);
}

/**
 * @brief  Records the last Windows error as the transport error.
 * @param  resp Response.
 * @param  what Step that failed.
 * @return False, for convenience.
 */
static bool fail(http_response *resp, const char *what){
  snprintf(resp->error, sizeof(resp->error), "%s failed (error %lu).", what, (unsigned long) GetLastError());
  return false;
}

bool http_perform(const http_request_spec *req, http_response *resp){
  memset(resp, 0, sizeof(*resp));
  static wchar_t url[4096], host[256], path[4096], method[16], header[1024];
  if(!to_wide(req->url, url, 4096) || !to_wide(req->method, method, 16)){
    snprintf(resp->error, sizeof(resp->error), "Invalid URL.");
    return false;
  }

  URL_COMPONENTS parts;
  memset(&parts, 0, sizeof(parts));
  parts.dwStructSize = sizeof(parts);
  parts.lpszHostName = host;
  parts.dwHostNameLength = 256;
  parts.lpszUrlPath = path;
  parts.dwUrlPathLength = 4096;
  if(!WinHttpCrackUrl(url, 0, 0, &parts) ||
     (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS)){
    snprintf(resp->error, sizeof(resp->error), "Only http and https URLs are supported.");
    return false;
  }

  HINTERNET session = WinHttpOpen(L"FileEncryptor", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  /* Automatic proxy detection needs Windows 8.1; older versions use the default. */
  if(!session) session = WinHttpOpen(L"FileEncryptor", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if(!session) return fail(resp, "WinHttpOpen");
  HINTERNET connection = WinHttpConnect(session, host, parts.nPort, 0);
  HINTERNET request = connection ? WinHttpOpenRequest(connection, method, path, NULL, WINHTTP_NO_REFERER,
                                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                      parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
                               : NULL;
  bool ok = request != NULL;
  if(!ok) fail(resp, "WinHttpOpenRequest");

  if(ok){
    DWORD never = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(request, WINHTTP_OPTION_REDIRECT_POLICY, &never, sizeof(never));
    WinHttpSetTimeouts(request, 15000, 15000, 30000, 30000);
    for(int i = 0; ok && req->headers && req->headers[i]; i++)
      ok = to_wide(req->headers[i], header, 1024) &&
           WinHttpAddRequestHeaders(request, header, (DWORD) -1L, WINHTTP_ADDREQ_FLAG_ADD);
    if(!ok) fail(resp, "WinHttpAddRequestHeaders");
  }

  if(ok){
    /* The total length is a DWORD: larger bodies send Content-Length themselves. */
    DWORD total = (DWORD) req->body_len;
    if(req->body_len > 0xFFFFFFFFull){
      swprintf(header, 1024, L"Content-Length: %llu", (unsigned long long) req->body_len);
      WinHttpAddRequestHeaders(request, header, (DWORD) -1L, WINHTTP_ADDREQ_FLAG_ADD);
      total = WINHTTP_IGNORE_REQUEST_TOTAL_LENGTH;
    }
    ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, total, 0);
    if(!ok) fail(resp, "WinHttpSendRequest");
  }
  for(uint64_t sent = 0; ok && sent < req->body_len; ){
    DWORD n = req->body_len - sent < sizeof(transfer_chunk) ? (DWORD) (req->body_len - sent) : sizeof(transfer_chunk);
    DWORD written = 0;
    ok = read_at(req->body_fd, transfer_chunk, n, sent) && WinHttpWriteData(request, transfer_chunk, n, &written) &&
         written == n;
    if(!ok) fail(resp, "WinHttpWriteData");
    sent += n;
  }
  if(ok && !(ok = WinHttpReceiveResponse(request, NULL))) fail(resp, "WinHttpReceiveResponse");

  if(ok){
    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    resp->status = (int) status;
    query_header(request, L"Content-Range", resp->content_range, sizeof(resp->content_range));
    query_header(request, L"ETag", resp->etag, sizeof(resp->etag));
  }
  while(ok && strcmp(req->method, "HEAD") != 0){
    DWORD n = 0;
    if(!WinHttpReadData(request, transfer_chunk, sizeof(transfer_chunk), &n)){
      ok = fail(resp, "WinHttpReadData");
      break;
    }
    if(n == 0) break;
    if(req->sink && !req->sink(req->sink_ctx, transfer_chunk, n)){
      snprintf(resp->error, sizeof(resp->error), "The response was rejected.");
      ok = false;
    }
  }

  if(request) WinHttpCloseHandle(request);
  if(connection) WinHttpCloseHandle(connection);
  WinHttpCloseHandle(session);
  return ok;
}

}
