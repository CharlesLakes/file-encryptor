/**
 * @file    server.cpp
 * @brief   Implementation of the .crls HTTP server (see server.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "server/server.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <time.h>
#include <unistd.h>
#include "core/append.h"
#include "core/archive.h"
#include "core/browser.h"
#include "core/fs.h"
#include "core/httpmsg.h"
#include "core/net.h"
#include "core/util.h"
#include "lib/heap_buffer.h"

namespace crls{

/** @brief Slowest accepted upload, in bytes per second. */
inline constexpr uint64_t MIN_BODY_RATE = 16 * 1024;

/** @brief Scratch buffer used to stream archives. */
static unsigned char send_chunk[CHUNK];

/* ---------------- Responses ---------------- */

/**
 * @brief  Sends a status line and headers.
 * @param  s      Socket.
 * @param  status Status code.
 * @param  type   Content-Type, or NULL.
 * @param  length Content-Length.
 * @param  extra  Additional header lines, each ending in CRLF (may be "").
 * @return True on success, false otherwise.
 */
static bool send_head(net_socket s, int status, const char *type, uint64_t length, const char *extra){
  char head[1024];
  int n = snprintf(head, sizeof(head),
                   "HTTP/1.1 %d %s\r\nServer: FileEncryptor\r\nConnection: close\r\n"
                   "Content-Length: %llu\r\n%s%s%s%s\r\n",
                   status, http_reason(status), (unsigned long long) length,
                   type ? "Content-Type: " : "", type ? type : "", type ? "\r\n" : "", extra);
  return n > 0 && (size_t) n < sizeof(head) && net_send_all(s, head, (size_t) n);
}

/**
 * @brief  Sends a complete plain-text response.
 * @param  s         Socket.
 * @param  status    Status code.
 * @param  extra     Additional header lines (may be "").
 * @param  head_only True for HEAD requests (no body).
 * @return The status code, for logging.
 */
static int send_error(net_socket s, int status, const char *extra, bool head_only){
  char body[128];
  int n = snprintf(body, sizeof(body), "%d %s\n", status, http_reason(status));
  send_head(s, status, "text/plain; charset=utf-8", (uint64_t) n, extra);
  if(!head_only) net_send_all(s, body, (size_t) n);
  return status;
}

/* ---------------- Names ---------------- */

/**
 * @brief  Checks that a decoded request path names a servable archive: ASCII
 *         letters, digits, '.', '_', '-' and spaces only, not starting with
 *         '.', ending in .crls and safe as a file name.
 * @param  name Decoded path.
 * @return True if the name is accepted.
 */
static bool valid_archive_name(const char *name){
  size_t len = strlen(name);
  if(len == 0 || len > MAX_NAME || name[0] == '.' || !has_crls_ext(name) || !fs_is_plain_name(name)) return false;
  for(const char *p = name; *p; p++){
    char c = *p;
    bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-' || c == ' ';
    if(!ok) return false;
  }
  return true;
}

/**
 * @brief  Builds the quoted ETag of an open archive.
 * @param  fd       File descriptor.
 * @param  out      Destination buffer.
 * @param  out_size Capacity of out.
 * @return True on success, false otherwise.
 */
static bool make_etag(int fd, char *out, size_t out_size){
  std::optional<std::string> id = fs_file_id(fd);
  if(!id) return false;
  snprintf(out, out_size, "\"%s\"", id->c_str());
  return true;
}

/**
 * @brief  Checks the If-Match header of a request against an ETag.
 * @param  req  Request.
 * @param  etag Current ETag, quoted.
 * @return True if there is no If-Match or it matches.
 */
static bool if_match_ok(const http_request *req, const char *etag){
  std::optional<std::string_view> value = http_header_value(*req, "If-Match");
  return !value || *value == "*" || *value == etag;
}

/* ---------------- GET and HEAD ---------------- */

/**
 * @brief  Answers GET / with the names of the archives in the folder.
 * @param  cfg       Settings.
 * @param  s         Socket.
 * @param  head_only True for HEAD.
 * @return Status code.
 */
static int serve_listing(const server_config &cfg, net_socket s, bool head_only){
  browser b;
  b.set_dir(cfg.dir);
  b.load();

  /* The size depends on the folder, so the allocation is checked. */
  size_t size = 1, len = 0;
  for(const dir_item &item : b.items()) size += item.name.size() + 1;
  heap_buffer text;
  if(!text.allocate(size)) return send_error(s, 500, "", head_only);
  for(const dir_item &item : b.items()){
    if(item.is_dir || !valid_archive_name(item.name.c_str())) continue;
    memcpy(text.data() + len, item.name.data(), item.name.size());
    len += item.name.size();
    text.data()[len++] = '\n';
  }

  send_head(s, 200, "text/plain; charset=utf-8", len, "Cache-Control: no-cache\r\n");
  if(!head_only) net_send_all(s, text.data(), len);
  return 200;
}

/**
 * @brief  Answers GET or HEAD for one archive, honoring Range and If-Match.
 * @param  cfg       Settings.
 * @param  s         Socket.
 * @param  req       Request.
 * @param  name      Validated archive name.
 * @param  head_only True for HEAD.
 * @return Status code.
 */
static int serve_archive(const server_config &cfg, net_socket s, const http_request *req,
                         const char *name, bool head_only){
  char etag[80], extra[512];
  unique_fd fd = fs_open_regular(join_path(cfg.dir, name), false);
  if(!fd) return send_error(s, 404, "", head_only);
  if(!make_etag(fd.get(), etag, sizeof(etag))){
    return send_error(s, 500, "", head_only);
  }
  if(!if_match_ok(req, etag)){
    return send_error(s, 412, "", head_only);
  }

  uint64_t size = file_size(fd.get()), first = 0, last = size ? size - 1 : 0;
  int status = 200;
  if(std::optional<std::string_view> range_header = http_header_value(*req, "Range")){
    std::optional<http_range> range = http_parse_range(*range_header, size);
    if(!range){
      snprintf(extra, sizeof(extra), "Content-Range: bytes */%llu\r\n", (unsigned long long) size);
      return send_error(s, 416, extra, head_only);
    }
    first = range->first;
    last = range->last;
    status = 206;
  }
  uint64_t length = size ? last - first + 1 : 0;

  /* On Windows a file locked by the GUI cannot be read: say so before any header. */
  if(length > 0 && !read_at(fd.get(), std::span(send_chunk, 1), first)){
    return send_error(s, 503, "Retry-After: 5\r\n", head_only);
  }

  int n = snprintf(extra, sizeof(extra), "Accept-Ranges: bytes\r\nETag: %s\r\nCache-Control: no-cache\r\n", etag);
  if(status == 206)
    snprintf(extra + n, sizeof(extra) - (size_t) n, "Content-Range: bytes %llu-%llu/%llu\r\n",
             (unsigned long long) first, (unsigned long long) last, (unsigned long long) size);
  bool ok = send_head(s, status, "application/octet-stream", length, extra);
  for(uint64_t sent = 0; ok && !head_only && sent < length; ){
    size_t chunk = length - sent < sizeof(send_chunk) ? (size_t) (length - sent) : sizeof(send_chunk);
    ok = read_at(fd.get(), std::span(send_chunk, chunk), first + sent) && net_send_all(s, send_chunk, chunk);
    sent += chunk;
  }
  return status;
}

/* ---------------- POST ---------------- */

/**
 * @brief Reads a request body: first the bytes that arrived with the head, then
 *        from the connection, until a deadline.
 */
class body_reader{
public:
  body_reader(net_socket s, std::string_view pending, time_t deadline) noexcept
    : s_(s), pending_(pending), deadline_(deadline) {}

  /** @brief Fills buf completely; false if the body ended, failed or took too long. */
  bool operator()(std::span<unsigned char> buf){
    size_t take = buf.size() < pending_.size() ? buf.size() : pending_.size();
    memcpy(buf.data(), pending_.data(), take);
    pending_.remove_prefix(take);
    for(size_t got = take; got < buf.size(); ){
      if(time(NULL) > deadline_) return false;
      long n = net_recv(s_, buf.data() + got, buf.size() - got);
      if(n <= 0) return false;
      got += static_cast<size_t>(n);
    }
    return true;
  }

private:
  net_socket s_;
  std::string_view pending_;
  time_t deadline_;
};

/**
 * @brief  Reads the claim of a signed append from the request headers.
 * @param  req Request.
 * @param  c   Output: the claim.
 * @return True if every header is present and well formed.
 */
static bool read_claim(const http_request *req, append_claim *c){
  *c = append_claim();
  c->content_length = req->content_length;
  std::optional<std::string_view> base = http_header_value(*req, "X-Crls-Base");
  std::optional<std::string_view> index_length = http_header_value(*req, "X-Crls-Index-Length");
  std::optional<std::string_view> nonce = http_header_value(*req, "X-Crls-Index-Nonce");
  std::optional<std::string_view> hash = http_header_value(*req, "X-Crls-Body-Hash");
  std::optional<std::string_view> sig = http_header_value(*req, "X-Crls-Sig");
  if(!base || !index_length || !nonce || !hash || !sig) return false;
  std::optional<uint64_t> base_value = http_parse_u64(*base), index_value = http_parse_u64(*index_length);
  if(!base_value || !index_value) return false;
  c->base = *base_value;
  c->index_length = *index_value;
  return http_hex_decode(*nonce, c->index_nonce) && http_hex_decode(*hash, c->body_hash) &&
         http_hex_decode(*sig, c->signature);
}

/**
 * @brief  Maps the result of an append step to an HTTP status code.
 * @param  r Result.
 * @return Status code.
 */
static int append_status(append_result r){
  switch(r){
    case append_result::ok:          return 204;
    case append_result::bad_request: return 400;
    case append_result::forbidden:   return 403;
    case append_result::conflict:    return 409;
    case append_result::too_large:   return 413;
    case append_result::unsupported: return 422;
    case append_result::io:          return 500;
  }
  return 500;
}

/**
 * @brief  Answers a signed append (see core/append.h). The signature is
 *         checked before any byte of the body is read.
 * @param  cfg         Settings.
 * @param  s           Socket.
 * @param  req         Request.
 * @param  name        Validated archive name.
 * @param  pending     Body bytes received with the head.
 * @param  pending_len Number of pending bytes.
 * @return Status code.
 */
static int serve_append(const server_config &cfg, net_socket s, const http_request *req, const char *name,
                        const char *pending, size_t pending_len){
  append_claim claim;
  if(!req->has_length) return send_error(s, 411, "", false);
  if(!read_claim(req, &claim)) return send_error(s, 400, "", false);
  if(claim.content_length > cfg.max_upload) return send_error(s, 413, "", false);

  char etag[80];
  unique_fd fd = fs_open_regular(join_path(cfg.dir, name), true);
  if(!fd) return send_error(s, 404, "", false);
  if(!make_etag(fd.get(), etag, sizeof(etag)) || !if_match_ok(req, etag)){
    return send_error(s, 412, "", false);
  }
  if(!fs_lock(fd.get())){
    return send_error(s, 423, "", false);
  }

  append_state st;
  append_result r = append_load_state(fd.get(), st);
  if(r == append_result::ok) r = append_check(st, claim, cfg.max_upload);
  if(r == append_result::ok){
    if(http_header_value(*req, "Expect") == std::optional<std::string_view>("100-continue"))
      net_send_all(s, "HTTP/1.1 100 Continue\r\n\r\n", 25);
    body_reader reader(s, std::string_view(pending, pending_len),
                       time(NULL) + cfg.timeout + static_cast<time_t>(claim.content_length / MIN_BODY_RATE));
    r = append_commit(fd.get(), claim, reader);
  }
  return send_error(s, append_status(r), "", false);
}

/* ---------------- Connections ---------------- */

/**
 * @brief  Reads one request from a connection and answers it.
 * @param  cfg    Settings.
 * @param  s      Socket.
 * @param  method Output: method, for logging.
 * @param  target Output: target, for logging.
 * @return Status code, or 0 if nothing could be answered.
 */
static int handle_connection(const server_config &cfg, net_socket s, char *method, char *target){
  static char head[http_max_head];
  http_request req;
  http_parse parsed = http_parse::incomplete;
  size_t len = 0;
  time_t deadline = time(NULL) + cfg.timeout;

  while(parsed == http_parse::incomplete){
    if(len == sizeof(head)){
      parsed = http_parse::too_large;
      break;
    }
    long n = net_recv(s, head + len, sizeof(head) - len);
    if(n <= 0 || time(NULL) > deadline) return len ? send_error(s, 408, "", false) : 0;
    len += (size_t) n;
    parsed = http_parse_request(std::string_view(head, len), req);
  }
  if(parsed == http_parse::too_large) return send_error(s, 431, "", false);
  if(parsed == http_parse::unsupported) return send_error(s, 501, "", false);
  if(parsed != http_parse::ok) return send_error(s, 400, "", false);
  /* The target is truncated for the log. */
  snprintf(method, 16, "%.*s", static_cast<int>(req.method.size()), req.method.data());
  snprintf(target, 256, "%.*s", static_cast<int>(req.target.size() < 255 ? req.target.size() : 255), req.target.data());

  bool get = req.method == "GET", head_only = req.method == "HEAD", post = req.method == "POST";
  if(!get && !head_only && !post) return send_error(s, 405, "Allow: GET, HEAD, POST\r\n", false);

  std::string name;
  if(!http_decode_path(req.target, name)) return send_error(s, 400, "", head_only);
  if(name.empty()){
    if(post) return send_error(s, 405, "Allow: GET, HEAD\r\n", false);
    return serve_listing(cfg, s, head_only);
  }
  if(!valid_archive_name(name.c_str())) return send_error(s, 404, "", head_only);
  if(post) return serve_append(cfg, s, &req, name.c_str(), head + req.head_length, len - req.head_length);
  return serve_archive(cfg, s, &req, name.c_str(), head_only);
}

int server_run(const server_config &cfg){
  if(!net_init()){
    fprintf(stderr, "Could not initialize the network.\n");
    return 1;
  }
  int port = 0;
  unique_socket listener = net_listen(cfg.bind.c_str(), cfg.port, &port);
  if(!listener){
    fprintf(stderr, "Could not listen on %s port %d.\n", cfg.bind.c_str(), cfg.port);
    return 1;
  }
  if(!cfg.port_file.empty()){
    FILE *f = fopen(cfg.port_file.c_str(), "w");
    if(f){
      fprintf(f, "%d\n", port);
      fclose(f);
    }
  }
  printf("Serving %s on http://%s:%d/\n", cfg.dir.c_str(), cfg.bind.c_str(), port);
  fflush(stdout);

  while(!cfg.stop || !cfg.stop->load()){
    unique_socket s = net_accept(listener.get());
    if(!s) continue;
    net_set_timeout(s.get(), cfg.timeout);
    char method[16] = "-", target[256] = "-";
    int status = handle_connection(cfg, s.get(), method, target);
    s = unique_socket();
    if(status) printf("%s %s %d\n", method, target, status);
    fflush(stdout);
  }
  return 0;
}

/**
 * @brief Prints the usage of the server options.
 */
static void server_usage(){
  const server_config defaults;
  fprintf(stderr, "usage: file-encryptor --server <folder> [--port N] [--bind ADDRESS]\n"
                  "                      [--max-upload BYTES] [--timeout SECONDS] [--port-file PATH]\n"
                  "defaults: --port %d --bind %s --max-upload %llu --timeout %d\n",
          defaults.port, defaults.bind.c_str(), static_cast<unsigned long long>(defaults.max_upload), defaults.timeout);
}

int server_main(int argc, char **argv){
  server_config cfg;
  if(argc < 2){
    server_usage();
    return 2;
  }
  std::optional<std::string> dir = fs_absolute_path(argv[1]);
  if(!dir){
    fprintf(stderr, "Could not find the folder %s.\n", argv[1]);
    return 2;
  }
  cfg.dir = std::move(*dir);

  /* Every option takes a value: they come in pairs. */
  for(int i = 2; i < argc; i += 2){
    const char *opt = argv[i], *value = i + 1 < argc ? argv[i + 1] : NULL;
    std::optional<uint64_t> parsed = value ? http_parse_u64(value) : std::nullopt;
    bool numeric = parsed.has_value();
    uint64_t number = parsed.value_or(0);
    if(strcmp(opt, "--port") == 0 && numeric && number <= 65535) cfg.port = (int) number;
    else if(strcmp(opt, "--bind") == 0 && value) cfg.bind = value;
    else if(strcmp(opt, "--max-upload") == 0 && numeric) cfg.max_upload = number;
    else if(strcmp(opt, "--timeout") == 0 && numeric && number > 0 && number < 3600) cfg.timeout = (int) number;
    else if(strcmp(opt, "--port-file") == 0 && value) cfg.port_file = value;
    else{
      server_usage();
      return 2;
    }
  }
  return server_run(cfg);
}

}
