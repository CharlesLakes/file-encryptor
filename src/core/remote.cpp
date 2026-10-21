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
#include <memory>
#include <optional>
#include <string>
#include "core/append.h"
#include "core/http.h"
#include "core/httpmsg.h"
#include "core/status.h"
#include "core/util.h"
#include "lib/heap_buffer.h"

namespace crls{

/** @brief Uploads retried on top of newer states before giving up. */
inline constexpr int MAX_UPLOAD_ATTEMPTS = 5;

/**
 * @brief byte_source over HTTP Range requests, with a cache of one block.
 */
class remote_source final : public byte_source{
public:
  /**
   * @brief  Prepares the cache and fetches the first block of url, learning
   *         the size and the ETag of the archive.
   * @param  url URL of the archive.
   * @return True on success, false otherwise (the status explains why).
   */
  [[nodiscard]] bool open(const std::string &url){
    url_ = url;
    if(!block_.allocate(REMOTE_BLOCK)){
      set_status(true, "Not enough memory.");
      return false;
    }
    return refresh();
  }

  bool read_at(std::span<unsigned char> buf, uint64_t offset) override;
  uint64_t size() const override{ return size_; }
  remote_source* as_remote() noexcept override{ return this; }

  /**
   * @brief  Fetches the first block again, learning the current size and ETag.
   *         The cache then holds the current header.
   * @return True on success, false otherwise.
   */
  [[nodiscard]] bool refresh();

  /**
   * @brief Records that the archive grew to new_size after an upload. The
   *        cached header pointed to the old index, so the cache is dropped.
   * @param new_size New size of the archive.
   */
  void grew(uint64_t new_size) noexcept{
    size_ = new_size;
    block_len_ = 0;
  }

  /** @brief URL of the archive. */
  const std::string& url() const noexcept{ return url_; }

  /** @brief ETag of the archive, sent in If-Match (may be empty). */
  const std::string& etag() const noexcept{ return etag_; }

private:
  bool fetch_range(unsigned char *buf, uint64_t first, size_t len, bool check_etag, size_t *got);

  std::string url_;                    /**< URL of the archive. */
  std::string etag_;                   /**< ETag of the archive. */
  uint64_t size_ = 0;                  /**< Size of the archive. */
  heap_buffer block_;                  /**< Cached bytes. */
  uint64_t block_start_ = 0;           /**< Offset of the cached bytes. */
  size_t block_len_ = 0;               /**< Number of cached bytes (0 = empty). */
};

bool remote_is_url(std::string_view s){
  auto starts_nocase = [&](std::string_view prefix){
    return s.size() >= prefix.size() && strncasecmp(s.data(), prefix.data(), prefix.size()) == 0;
  };
  return starts_nocase("http://") || starts_nocase("https://");
}

/**
 * @brief  Fetches bytes first..first+len-1 of the archive with a Range request.
 *
 * The response must be 206 with exactly the requested Content-Range and body
 * length. With check_etag false (first request, reload) the ETag and size are
 * learned from the response; otherwise If-Match is sent and the total size
 * must not change.
 *
 * @param  buf        Destination buffer of len bytes.
 * @param  first      First byte.
 * @param  len        Number of bytes; on the first request, the most wanted.
 * @param  check_etag True to send If-Match and require the known size.
 * @param  got        Output: number of bytes received.
 * @return True on success, false otherwise (the status explains why).
 */
bool remote_source::fetch_range(unsigned char *buf, uint64_t first, size_t len, bool check_etag, size_t *got){
  /* Copy the body into buf and reject anything beyond the requested length. */
  size_t received = 0;
  auto sink = [&](std::span<const unsigned char> data){
    if(data.size() > len - received) return false;
    memcpy(buf + received, data.data(), data.size());
    received += data.size();
    return true;
  };
  http_request_spec req;
  req.url = url_;
  req.headers.push_back("Range: bytes=" + std::to_string(first) + "-" + std::to_string(first + len - 1));
  if(check_etag && !etag_.empty()) req.headers.push_back("If-Match: " + etag_);
  req.sink = sink;
  http_response resp;
  if(!http_perform(req, resp)){
    set_status(true, "Could not download %s: %s", url_.c_str(), resp.error.c_str());
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
    set_status(true, "The server answered %d %s for %s.", resp.status, http_reason(resp.status), url_.c_str());
    return false;
  }

  std::optional<http_content_range> range = http_parse_content_range(resp.content_range);
  if(!range || range->first != first || range->last - range->first + 1 != received ||
     (check_etag && range->total != size_)){
    set_status(true, "The server sent an unexpected range for %s.", url_.c_str());
    return false;
  }
  if(!check_etag){
    size_ = range->total;
    etag_ = resp.etag;
  }
  *got = received;
  return true;
}

bool remote_source::read_at(std::span<unsigned char> buf, uint64_t offset){
  size_t len = buf.size();
  if(len == 0) return true;
  if(offset > size_ || len > size_ - offset) return false;
  if(block_len_ && offset >= block_start_ && offset + len <= block_start_ + block_len_){
    memcpy(buf.data(), block_.data() + (offset - block_start_), len);
    return true;
  }

  size_t got;
  if(len > REMOTE_BLOCK) return fetch_range(buf.data(), offset, len, true, &got);
  size_t want = size_ - offset < REMOTE_BLOCK ? (size_t) (size_ - offset) : REMOTE_BLOCK;
  block_len_ = 0;
  if(!fetch_range(block_.data(), offset, want, true, &got)) return false;
  block_start_ = offset;
  block_len_ = got;
  memcpy(buf.data(), block_.data(), len);
  return true;
}

bool remote_source::refresh(){
  size_t got;
  block_len_ = 0;
  if(!fetch_range(block_.data(), 0, REMOTE_BLOCK, false, &got)) return false;
  block_start_ = 0;
  block_len_ = got;
  return true;
}

bool remote_open(archive &a, const std::string &url, const char *password){
  a.close();
  std::unique_ptr<remote_source> src = std::make_unique<remote_source>();
  if(!src->open(url)) return false;
  return a.open_source(std::move(src), url, password);
}

/**
 * @brief  Sends a prepared upload.
 * @param  c  Remote state.
 * @param  up Prepared upload.
 * @param  resp Output: the response.
 * @return True if a response was received.
 */
static bool send_upload(remote_source *c, const append_upload *up, http_response *resp){
  http_request_spec req;
  req.method = "POST";
  req.url = c->url();
  req.headers = {"Content-Type: application/octet-stream",
                 "X-Crls-Base: " + std::to_string(up->claim.base),
                 "X-Crls-Index-Length: " + std::to_string(up->claim.index_length),
                 "X-Crls-Index-Nonce: " + http_hex_encode(up->claim.index_nonce),
                 "X-Crls-Body-Hash: " + http_hex_encode(up->claim.body_hash),
                 "X-Crls-Sig: " + http_hex_encode(up->claim.signature)};
  if(!c->etag().empty()) req.headers.push_back("If-Match: " + c->etag());
  req.body_fd = up->body_fd();
  req.body_len = up->claim.content_length;
  if(http_perform(req, *resp)) return true;
  set_status(true, "Could not upload to %s: %s", c->url().c_str(), resp->error.c_str());
  return false;
}

bool remote_add_file(archive &a, const std::string &path){
  remote_source *c = a.source() ? a.source()->as_remote() : nullptr;
  append_upload up;
  if(!a.is_remote() || !c){
    set_status(true, "This archive is not remote.");
    return false;
  }
  if(!up.prepare(a, path)) return false;

  bool ok = false;
  int attempt;
  for(attempt = 0; attempt < MAX_UPLOAD_ATTEMPTS; attempt++){
    http_response resp;
    if(!send_upload(c, &up, &resp)) break;
    if(resp.status == 204){
      up.applied(a);
      c->grew(up.claim.base + up.claim.content_length);
      ok = true;
      break;
    }
    if(resp.status == 409){
      /* Someone else uploaded first: build the index again on top of theirs. */
      if(!c->refresh() || !a.reload() || !up.rebase(a)) break;
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
    format_size(up.entry().size, size_text, sizeof(size_text));
    set_status(false, "Uploaded %s (%s).", up.entry().name.c_str(), size_text);
  }
  return ok;
}

}
