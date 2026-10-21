/**
 * @file    curl.h
 * @brief   Minimal hand-written libcurl declarations, loaded at run time.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section curl_overview Overview
 *
 * libcurl is opened with dlopen() (libcurl.so.4, or libcurl-gnutls.so.4 as
 * shipped by some distributions) instead of being linked, so the program still
 * starts, and local archives still work, on systems without it. No -dev
 * package is needed.
 *
 * curl_easy_setopt() and curl_easy_getinfo() are variadic: the pointer types
 * below are declared variadic too, because calling a variadic function through
 * a non-variadic pointer is undefined behavior. Without the type-checking
 * macros of the real header, every argument must have exactly the type the
 * option expects (long, a pointer, curl_off_t or a callback); http_curl.cpp only
 * passes them through typed wrappers.
 *
 * Option numbers are copied from include/curl/curl.h of libcurl 7.85+, where
 * each one is a type base (LONG 0, OBJECTPOINT 10000, FUNCTIONPOINT 20000,
 * OFF_T 30000) plus a fixed index.
 */

#ifndef DEPS_CURL_H
#define DEPS_CURL_H

#include <cstddef>
#include <cstdint>

/* The library is written in C: its symbols are not mangled. */
extern "C"{

typedef void CURL;
typedef int CURLcode;
typedef int64_t curl_off_t;

/** @brief Linked list of strings (request headers). */
struct curl_slist{
  char *data;
  struct curl_slist *next;
};

typedef size_t (*curl_write_callback)(char *ptr, size_t size, size_t nmemb, void *userdata);
typedef size_t (*curl_read_callback)(char *buffer, size_t size, size_t nitems, void *userdata);

/** @brief Functions of libcurl, resolved with dlsym(). */
struct curl_api{
  CURLcode           (*global_init)(long flags);
  CURL*              (*easy_init)();
  CURLcode           (*easy_setopt)(CURL *handle, int option, ...);
  CURLcode           (*easy_perform)(CURL *handle);
  CURLcode           (*easy_getinfo)(CURL *handle, int info, ...);
  void               (*easy_cleanup)(CURL *handle);
  const char*        (*easy_strerror)(CURLcode code);
  struct curl_slist* (*slist_append)(struct curl_slist *list, const char *string);
  void               (*slist_free_all)(struct curl_slist *list);
};

}

/* Constants of <curl/curl.h>, typed as the functions that take them expect. */
inline constexpr CURLcode CURLE_OK                 = 0;
inline constexpr CURLcode CURLE_UNKNOWN_OPTION     = 48;
inline constexpr long CURL_GLOBAL_DEFAULT          = 3L;
inline constexpr size_t CURL_ERROR_SIZE            = 256;

inline constexpr int CURLOPT_WRITEDATA             = 10001;  /* OBJECTPOINT + 1 */
inline constexpr int CURLOPT_URL                   = 10002;  /* OBJECTPOINT + 2 */
inline constexpr int CURLOPT_READDATA              = 10009;  /* OBJECTPOINT + 9 */
inline constexpr int CURLOPT_ERRORBUFFER           = 10010;  /* OBJECTPOINT + 10 */
inline constexpr int CURLOPT_WRITEFUNCTION         = 20011;  /* FUNCTIONPOINT + 11 */
inline constexpr int CURLOPT_READFUNCTION          = 20012;  /* FUNCTIONPOINT + 12 */
inline constexpr int CURLOPT_LOW_SPEED_LIMIT       = 19;     /* LONG + 19 */
inline constexpr int CURLOPT_LOW_SPEED_TIME        = 20;     /* LONG + 20 */
inline constexpr int CURLOPT_HTTPHEADER            = 10023;  /* SLISTPOINT + 23 */
inline constexpr int CURLOPT_HEADERDATA            = 10029;  /* OBJECTPOINT + 29 */
inline constexpr int CURLOPT_NOBODY                = 44;     /* LONG + 44 */
inline constexpr int CURLOPT_POST                  = 47;     /* LONG + 47 */
inline constexpr int CURLOPT_FOLLOWLOCATION        = 52;     /* LONG + 52 */
inline constexpr int CURLOPT_CONNECTTIMEOUT        = 78;     /* LONG + 78 */
inline constexpr int CURLOPT_HEADERFUNCTION        = 20079;  /* FUNCTIONPOINT + 79 */
inline constexpr int CURLOPT_NOSIGNAL              = 99;     /* LONG + 99 */
inline constexpr int CURLOPT_PROTOCOLS             = 181;    /* LONG + 181 (deprecated in 7.85) */
inline constexpr int CURLOPT_POSTFIELDSIZE_LARGE   = 30120;  /* OFF_T + 120 */
inline constexpr int CURLOPT_PROTOCOLS_STR         = 10318;  /* STRINGPOINT + 318 (7.85+) */

inline constexpr long CURLPROTO_HTTP               = 1L << 0;
inline constexpr long CURLPROTO_HTTPS              = 1L << 1;

inline constexpr int CURLINFO_RESPONSE_CODE        = 0x200002; /* CURLINFO_LONG + 2 */

#endif
