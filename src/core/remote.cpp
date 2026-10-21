/**
 * @file    remote.cpp
 * @brief   Implementation of archives at URLs (see remote.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/remote.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "core/append.h"
#include "core/http.h"
#include "core/httpmsg.h"
#include "core/status.h"
#include "core/util.h"

namespace crls{

#define MAX_UPLOAD_ATTEMPTS 5

/** @brief State of a remote archive_source. */
typedef struct{
  char url[PATH_MAX];                  /**< URL of the archive. */
  char etag[128];                      /**< ETag of the archive, sent in If-Match. */
  uint64_t size;                       /**< Size of the archive. */
  unsigned char *block;                /**< Cached bytes. */
  uint64_t block_start;                /**< Offset of the cached bytes. */
  size_t block_len;                    /**< Number of cached bytes (0 = empty). */
}remote_ctx;

/** @brief Destination of a ranged GET. */
typedef struct{
  unsigned char *buf;                  /**< Where the bytes go. */
  size_t capacity;                     /**< Bytes expected. */
  size_t received;                     /**< Bytes received so far. */
}range_sink_ctx;

/** @brief http_sink that copies into a fixed buffer and rejects any excess. */
static bool range_sink(void *ctx, const void *data, size_t len){
  range_sink_ctx *r = (range_sink_ctx *) ctx;
  if(len > r->capacity - r->received) return false;
  memcpy(r->buf + r->received, data, len);
  r->received += len;
  return true;
}

bool remote_is_url(const char *s){
  return strncasecmp(s, "http://", 7) == 0 || strncasecmp(s, "https://", 8) == 0;
}

/**
 * @brief  Fetches bytes first..first+len-1 of the archive with a Range request.
 *
 * The response must be 206 with exactly the requested Content-Range and body
 * length. With check_etag false (first request, reload) the ETag and size are
 * learned from the response; otherwise If-Match is sent and the total size
 * must not change.
 *
 * @param  c          Remote state.
 * @param  buf        Destination buffer of len bytes.
 * @param  first      First byte.
 * @param  len        Number of bytes; on the first request, the most wanted.
 * @param  check_etag True to send If-Match and require the known size.
 * @param  got        Output: number of bytes received.
 * @return True on success, false otherwise (the status explains why).
 */
static bool fetch_range(remote_ctx *c, unsigned char *buf, uint64_t first, size_t len, bool check_etag, size_t *got){
  char range[96], if_match[160];
  snprintf(range, sizeof(range), "Range: bytes=%llu-%llu", (unsigned long long) first,
           (unsigned long long) (first + len - 1));
  snprintf(if_match, sizeof(if_match), "If-Match: %s", c->etag);
  const char *headers[] = {range, check_etag && c->etag[0] ? if_match : NULL, NULL};

  range_sink_ctx sink = {buf, len, 0};
  http_request_spec req = {"GET", c->url, headers, -1, 0, range_sink, &sink};
  http_response resp;
  if(!http_perform(&req, &resp)){
    set_status(true, "Could not download %s: %s", c->url, resp.error);
    return false;
  }
  if(resp.status == 412){
    set_status(true, "The archive changed on the server (it was compacted or replaced). Open it again.");
    return false;
  }
  if(resp.status == 200){
    set_status(true, "The server does not support byte ranges, which are needed to read archives.");
    return false;
  }
  if(resp.status != 206){
    set_status(true, "The server answered %d %s for %s.", resp.status, http_reason(resp.status), c->url);
    return false;
  }

  uint64_t r_first, r_last, total;
  if(!http_parse_content_range(resp.content_range, &r_first, &r_last, &total) || r_first != first ||
     r_last - r_first + 1 != sink.received || (check_etag && total != c->size)){
    set_status(true, "The server sent an unexpected range for %s.", c->url);
    return false;
  }
  if(!check_etag){
    c->size = total;
    snprintf(c->etag, sizeof(c->etag), "%s", resp.etag);
  }
  *got = sink.received;
  return true;
}

/** @brief archive_source read_at() over HTTP, through the block cache. */
static bool remote_read_at(archive_source *src, void *buf, size_t len, uint64_t offset){
  remote_ctx *c = (remote_ctx *) src->ctx;
  if(len == 0) return true;
  if(offset > c->size || len > c->size - offset) return false;
  if(c->block_len && offset >= c->block_start && offset + len <= c->block_start + c->block_len){
    memcpy(buf, c->block + (offset - c->block_start), len);
    return true;
  }

  size_t got;
  if(len > REMOTE_BLOCK) return fetch_range(c, (unsigned char *) buf, offset, len, true, &got);
  size_t want = c->size - offset < REMOTE_BLOCK ? (size_t) (c->size - offset) : REMOTE_BLOCK;
  c->block_len = 0;
  if(!fetch_range(c, c->block, offset, want, true, &got)) return false;
  c->block_start = offset;
  c->block_len = got;
  memcpy(buf, c->block, len);
  return true;
}

/** @brief archive_source size() of a remote archive. */
static uint64_t remote_size(archive_source *src){
  return ((remote_ctx *) src->ctx)->size;
}

/** @brief archive_source close() of a remote archive. */
static void remote_close(archive_source *src){
  remote_ctx *c = (remote_ctx *) src->ctx;
  if(!c) return;
  free(c->block);
  free(c);
}

/**
 * @brief  Fetches the first block of the archive again, learning its current
 *         size and ETag. The cache then holds the current header.
 * @param  c Remote state.
 * @return True on success, false otherwise.
 */
static bool refresh(remote_ctx *c){
  size_t got;
  c->block_len = 0;
  if(!fetch_range(c, c->block, 0, REMOTE_BLOCK, false, &got)) return false;
  c->block_start = 0;
  c->block_len = got;
  return true;
}

bool remote_open(archive *a, const char *url, const char *password){
  remote_ctx *c = (remote_ctx *) calloc(1, sizeof(remote_ctx));
  if(c) c->block = (unsigned char *) malloc(REMOTE_BLOCK);
  if(!c || !c->block || strlen(url) >= sizeof(c->url)){
    if(c) free(c->block);
    free(c);
    set_status(true, c ? "The URL is too long." : "Not enough memory.");
    return false;
  }
  snprintf(c->url, sizeof(c->url), "%s", url);

  memset(&a->src, 0, sizeof(a->src));
  a->src.read_at = remote_read_at;
  a->src.size = remote_size;
  a->src.close = remote_close;
  a->src.fd = -1;
  a->src.ctx = c;
  if(!refresh(c) || !archive_open_source(a, url, password)){
    source_close(&a->src);
    return false;
  }
  return true;
}

/**
 * @brief  Sends a prepared upload.
 * @param  c  Remote state.
 * @param  up Prepared upload.
 * @param  resp Output: the response.
 * @return True if a response was received.
 */
static bool send_upload(remote_ctx *c, const append_upload *up, http_response *resp){
  char base[64], index_length[64], nonce[32 + 2*NONCE_BYTES], hash[32 + 2*BODY_HASH_BYTES];
  char sig[32 + 2*SIGN_BYTES], if_match[160];
  char nonce_hex[2*NONCE_BYTES + 1], hash_hex[2*BODY_HASH_BYTES + 1], sig_hex[2*SIGN_BYTES + 1];
  snprintf(base, sizeof(base), "X-Crls-Base: %llu", (unsigned long long) up->claim.base);
  snprintf(index_length, sizeof(index_length), "X-Crls-Index-Length: %llu", (unsigned long long) up->claim.index_length);
  http_hex_encode(up->claim.index_nonce, NONCE_BYTES, nonce_hex);
  snprintf(nonce, sizeof(nonce), "X-Crls-Index-Nonce: %s", nonce_hex);
  http_hex_encode(up->claim.body_hash, BODY_HASH_BYTES, hash_hex);
  snprintf(hash, sizeof(hash), "X-Crls-Body-Hash: %s", hash_hex);
  http_hex_encode(up->claim.signature, SIGN_BYTES, sig_hex);
  snprintf(sig, sizeof(sig), "X-Crls-Sig: %s", sig_hex);
  snprintf(if_match, sizeof(if_match), "If-Match: %s", c->etag);
  const char *headers[] = {"Content-Type: application/octet-stream", base, index_length, nonce, hash, sig,
                           c->etag[0] ? if_match : NULL, NULL};

  http_request_spec req = {"POST", c->url, headers, up->body_fd, up->claim.content_length, NULL, NULL};
  if(http_perform(&req, resp)) return true;
  set_status(true, "Could not upload to %s: %s", c->url, resp->error);
  return false;
}

bool remote_add_file(archive *a, const char *path){
  remote_ctx *c = (remote_ctx *) a->src.ctx;
  append_upload up;
  if(!a->is_remote || !c){
    set_status(true, "This archive is not remote.");
    return false;
  }
  if(!append_prepare(a, path, &up)) return false;

  bool ok = false;
  int attempt;
  for(attempt = 0; attempt < MAX_UPLOAD_ATTEMPTS; attempt++){
    http_response resp;
    if(!send_upload(c, &up, &resp)) break;
    if(resp.status == 204){
      append_applied(a, &up);
      c->size = up.claim.base + up.claim.content_length;
      /* The cached header points to the old index. */
      c->block_len = 0;
      ok = true;
      break;
    }
    if(resp.status == 409){
      /* Someone else uploaded first: build the index again on top of theirs. */
      if(!refresh(c) || !archive_reload(a) || !append_rebase(a, &up)) break;
      continue;
    }
    switch(resp.status){
      case 403: set_status(true, "The server rejected the upload: the password does not match."); break;
      case 404: set_status(true, "The archive no longer exists on the server."); break;
      case 405: set_status(true, "This server does not accept uploads (it is not a FileEncryptor server)."); break;
      case 412: set_status(true, "The archive changed on the server (it was compacted or replaced). Open it again."); break;
      case 413: set_status(true, "The file is too large for the server."); break;
      case 422: set_status(true, "The archive on the server must be compacted to accept uploads."); break;
      case 423: set_status(true, "The archive is open on the server machine. Try again later."); break;
      default:  set_status(true, "The server answered %d %s.", resp.status, http_reason(resp.status)); break;
    }
    break;
  }
  if(attempt == MAX_UPLOAD_ATTEMPTS) set_status(true, "The archive kept changing on the server. Try again.");

  if(ok){
    char size_text[32];
    format_size(up.entry.size, size_text, sizeof(size_text));
    set_status(false, "Uploaded %s (%s).", up.entry.name, size_text);
  }
  append_discard(&up);
  return ok;
}

}
