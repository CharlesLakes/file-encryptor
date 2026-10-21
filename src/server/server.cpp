/**
 * @file    server.cpp
 * @brief   Implementation of the .crls HTTP server (see server.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "server/server.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include "core/append.h"
#include "core/archive.h"
#include "core/browser.h"
#include "core/fs.h"
#include "core/httpmsg.h"
#include "core/net.h"
#include "core/util.h"
#include "lib/heap_buffer.h"

namespace crls{

using steady_clock = std::chrono::steady_clock;

/** @brief Slowest accepted upload, in bytes per second. */
inline constexpr uint64_t MIN_BODY_RATE = 16 * 1024;

/** @brief Longest target written to the log. */
inline constexpr std::size_t MAX_LOGGED_TARGET = 255;

/* Scratch buffers, static because they are too large for the stack of a
   Windows thread; the server handles one connection at a time. */
static std::array<unsigned char, CHUNK> send_chunk;
static std::array<char, http_max_head> head_buffer;

/* ---------------- Responses ---------------- */

/**
 * @brief  Sends a status line and headers.
 * @param  s      Socket.
 * @param  status Status code.
 * @param  type   Content-Type, or empty for none.
 * @param  length Content-Length.
 * @param  extra  Additional header lines, each ending in CRLF (may be empty).
 * @return True on success, false otherwise.
 */
static bool send_head(net_socket s, int status, std::string_view type, uint64_t length, std::string_view extra){
  std::string head = "HTTP/1.1 " + std::to_string(status) + " " + http_reason(status) + "\r\n"
                     "Server: FileEncryptor\r\nConnection: close\r\n"
                     "Content-Length: " + std::to_string(length) + "\r\n";
  if(!type.empty()){
    head += "Content-Type: ";
    head += type;
    head += "\r\n";
  }
  head += extra;
  head += "\r\n";
  return net_send_all(s, head);
}

/**
 * @brief  Sends a complete plain-text response.
 * @param  s         Socket.
 * @param  status    Status code.
 * @param  extra     Additional header lines (may be empty).
 * @param  head_only True for HEAD requests (no body).
 * @return The status code, for logging.
 */
static int send_error(net_socket s, int status, std::string_view extra, bool head_only){
  std::string body = std::to_string(status) + " " + http_reason(status) + "\n";
  send_head(s, status, "text/plain; charset=utf-8", body.size(), extra);
  if(!head_only) net_send_all(s, body);
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
static bool valid_archive_name(std::string_view name){
  auto allowed = [](char c){
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-' || c == ' ';
  };
  return !name.empty() && name.size() <= MAX_NAME && !name.starts_with('.') && has_crls_ext(name) &&
         fs_is_plain_name(name) && std::ranges::all_of(name, allowed);
}

/**
 * @brief  Builds the quoted ETag of an open archive.
 * @param  fd File descriptor.
 * @return The ETag, or std::nullopt on failure.
 */
static std::optional<std::string> make_etag(int fd){
  std::optional<std::string> id = fs_file_id(fd);
  if(!id) return std::nullopt;
  return "\"" + *id + "\"";
}

/**
 * @brief  Checks the If-Match header of a request against an ETag.
 * @param  req  Request.
 * @param  etag Current ETag, quoted.
 * @return True if there is no If-Match or it matches.
 */
static bool if_match_ok(const http_request &req, std::string_view etag){
  std::optional<std::string_view> value = http_header_value(req, "If-Match");
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
  size_t size = 1;
  for(const dir_item &item : b.items()) size += item.name.size() + 1;
  heap_buffer text;
  if(!text.allocate(size)) return send_error(s, 500, "", head_only);
  byte_writer out(text.span());
  for(const dir_item &item : b.items()){
    if(item.is_dir || !valid_archive_name(item.name)) continue;
    out.bytes(item.name);
    out.bytes("\n");
  }

  send_head(s, 200, "text/plain; charset=utf-8", out.written(), "Cache-Control: no-cache\r\n");
  if(!head_only) net_send_all(s, text.span().first(out.written()));
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
static int serve_archive(const server_config &cfg, net_socket s, const http_request &req,
                         const std::string &name, bool head_only){
  unique_fd fd = fs_open_regular(join_path(cfg.dir, name), false);
  if(!fd) return send_error(s, 404, "", head_only);
  std::optional<std::string> etag = make_etag(fd.get());
  if(!etag) return send_error(s, 500, "", head_only);
  if(!if_match_ok(req, *etag)) return send_error(s, 412, "", head_only);

  uint64_t size = file_size(fd.get()), first = 0, last = size ? size - 1 : 0;
  int status = 200;
  if(std::optional<std::string_view> range_header = http_header_value(req, "Range")){
    std::optional<http_range> range = http_parse_range(*range_header, size);
    if(!range) return send_error(s, 416, "Content-Range: bytes */" + std::to_string(size) + "\r\n", head_only);
    first = range->first;
    last = range->last;
    status = 206;
  }
  uint64_t length = size ? last - first + 1 : 0;

  /* On Windows a file locked by the GUI cannot be read: say so before any header. */
  if(length > 0 && !read_at(fd.get(), std::span(send_chunk).first(1), first))
    return send_error(s, 503, "Retry-After: 5\r\n", head_only);

  std::string extra = "Accept-Ranges: bytes\r\nETag: " + *etag + "\r\nCache-Control: no-cache\r\n";
  if(status == 206)
    extra += "Content-Range: bytes " + std::to_string(first) + "-" + std::to_string(last) + "/" +
             std::to_string(size) + "\r\n";
  bool ok = send_head(s, status, "application/octet-stream", length, extra);
  for(uint64_t sent = 0; ok && !head_only && sent < length; ){
    std::span<unsigned char> chunk =
        std::span(send_chunk).first(static_cast<size_t>(std::min<uint64_t>(length - sent, send_chunk.size())));
    ok = read_at(fd.get(), chunk, first + sent) && net_send_all(s, chunk);
    sent += chunk.size();
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
  body_reader(net_socket s, std::string_view pending, steady_clock::time_point deadline) noexcept
    : s_(s), pending_(pending), deadline_(deadline) {}

  /** @brief Fills buf completely; false if the body ended, failed or took too long. */
  bool operator()(std::span<unsigned char> buf){
    size_t take = std::min(buf.size(), pending_.size());
    std::ranges::copy(pending_.substr(0, take), buf.begin());
    pending_.remove_prefix(take);
    for(size_t got = take; got < buf.size(); ){
      if(steady_clock::now() > deadline_) return false;
      std::optional<size_t> n = net_recv(s_, buf.subspan(got));
      if(!n || *n == 0) return false;
      got += *n;
    }
    return true;
  }

private:
  net_socket s_;
  std::string_view pending_;
  steady_clock::time_point deadline_;
};

/**
 * @brief  Reads the claim of a signed append from the request headers.
 * @param  req Request.
 * @return The claim, or std::nullopt if a header is missing or malformed.
 */
static std::optional<append_claim> read_claim(const http_request &req){
  std::optional<std::string_view> base = http_header_value(req, "X-Crls-Base");
  std::optional<std::string_view> index_length = http_header_value(req, "X-Crls-Index-Length");
  std::optional<std::string_view> nonce = http_header_value(req, "X-Crls-Index-Nonce");
  std::optional<std::string_view> hash = http_header_value(req, "X-Crls-Body-Hash");
  std::optional<std::string_view> sig = http_header_value(req, "X-Crls-Sig");
  if(!base || !index_length || !nonce || !hash || !sig) return std::nullopt;
  std::optional<uint64_t> base_value = http_parse_u64(*base), index_value = http_parse_u64(*index_length);
  if(!base_value || !index_value) return std::nullopt;

  append_claim c;
  c.content_length = req.content_length;
  c.base = *base_value;
  c.index_length = *index_value;
  if(!http_hex_decode(*nonce, c.index_nonce) || !http_hex_decode(*hash, c.body_hash) ||
     !http_hex_decode(*sig, c.signature)) return std::nullopt;
  return c;
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
 * @param  cfg     Settings.
 * @param  s       Socket.
 * @param  req     Request.
 * @param  name    Validated archive name.
 * @param  pending Body bytes received with the head.
 * @return Status code.
 */
static int serve_append(const server_config &cfg, net_socket s, const http_request &req, const std::string &name,
                        std::string_view pending){
  if(!req.has_length) return send_error(s, 411, "", false);
  std::optional<append_claim> claim = read_claim(req);
  if(!claim) return send_error(s, 400, "", false);
  if(claim->content_length > cfg.max_upload) return send_error(s, 413, "", false);

  unique_fd fd = fs_open_regular(join_path(cfg.dir, name), true);
  if(!fd) return send_error(s, 404, "", false);
  std::optional<std::string> etag = make_etag(fd.get());
  if(!etag || !if_match_ok(req, *etag)) return send_error(s, 412, "", false);
  if(!fs_lock(fd.get())) return send_error(s, 423, "", false);

  append_state st;
  append_result r = append_load_state(fd.get(), st);
  if(r == append_result::ok) r = append_check(st, *claim, cfg.max_upload);
  if(r == append_result::ok){
    if(http_header_value(req, "Expect") == std::optional<std::string_view>("100-continue"))
      net_send_all(s, "HTTP/1.1 100 Continue\r\n\r\n");
    steady_clock::time_point deadline = steady_clock::now() + std::chrono::seconds(cfg.timeout) +
                                 std::chrono::seconds(claim->content_length / MIN_BODY_RATE);
    body_reader reader(s, pending, deadline);
    r = append_commit(fd.get(), *claim, reader);
  }
  return send_error(s, append_status(r), "", false);
}

/* ---------------- Connections ---------------- */

/** @brief What the log line of a connection shows. */
struct request_log{
  int status = 0;            /**< Status code, or 0 if nothing could be answered. */
  std::string method = "-";  /**< Request method. */
  std::string target = "-";  /**< Request target, truncated. */
};

/**
 * @brief  Answers one request from a connection.
 * @param  cfg Settings.
 * @param  s   Socket.
 * @param  log Output: method and target, once the request line is parsed.
 * @return Status code, or 0 if nothing could be answered.
 */
static int answer_request(const server_config &cfg, net_socket s, request_log &log){
  http_request req;
  http_parse parsed = http_parse::incomplete;
  size_t len = 0;
  steady_clock::time_point deadline = steady_clock::now() + std::chrono::seconds(cfg.timeout);

  while(parsed == http_parse::incomplete){
    if(len == head_buffer.size()){
      parsed = http_parse::too_large;
      break;
    }
    std::optional<size_t> n = net_recv(s, std::span(head_buffer).subspan(len));
    if(!n || *n == 0 || steady_clock::now() > deadline) return len ? send_error(s, 408, "", false) : 0;
    len += *n;
    parsed = http_parse_request(std::string_view(head_buffer.data(), len), req);
  }
  if(parsed == http_parse::too_large) return send_error(s, 431, "", false);
  if(parsed == http_parse::unsupported) return send_error(s, 501, "", false);
  if(parsed != http_parse::ok) return send_error(s, 400, "", false);
  log.method = req.method;
  log.target = req.target.substr(0, MAX_LOGGED_TARGET);

  bool get = req.method == "GET", head_only = req.method == "HEAD", post = req.method == "POST";
  if(!get && !head_only && !post) return send_error(s, 405, "Allow: GET, HEAD, POST\r\n", false);

  std::optional<std::string> name = http_decode_path(req.target);
  if(!name) return send_error(s, 400, "", head_only);
  if(name->empty()){
    if(post) return send_error(s, 405, "Allow: GET, HEAD\r\n", false);
    return serve_listing(cfg, s, head_only);
  }
  if(!valid_archive_name(*name)) return send_error(s, 404, "", head_only);
  if(post){
    std::string_view pending(head_buffer.data() + req.head_length, len - req.head_length);
    return serve_append(cfg, s, req, *name, pending);
  }
  return serve_archive(cfg, s, req, *name, head_only);
}

/**
 * @brief  Reads one request from a connection and answers it.
 * @param  cfg Settings.
 * @param  s   Socket.
 * @return The status code with the method and target, for the log.
 */
static request_log handle_connection(const server_config &cfg, net_socket s){
  request_log log;
  log.status = answer_request(cfg, s, log);
  return log;
}

/**
 * @brief  Writes the bound port to a file, for scripts and tests that start
 *         the server on port 0.
 * @param  path Path of the file.
 * @param  port Port.
 */
static void write_port_file(const std::string &path, int port){
  struct file_closer{
    void operator()(FILE *f) const noexcept{ fclose(f); }
  };
  std::unique_ptr<FILE, file_closer> f(fopen(path.c_str(), "w"));
  if(f) fprintf(f.get(), "%d\n", port);
}

int server_run(const server_config &cfg){
  if(!net_init()){
    fprintf(stderr, "Could not initialize the network.\n");
    return 1;
  }
  net_listener listener = net_listen(cfg.bind, cfg.port);
  if(!listener.socket){
    fprintf(stderr, "Could not listen on %s port %d.\n", cfg.bind.c_str(), cfg.port);
    return 1;
  }
  if(!cfg.port_file.empty()) write_port_file(cfg.port_file, listener.port);
  printf("Serving %s on http://%s:%d/\n", cfg.dir.c_str(), cfg.bind.c_str(), listener.port);
  fflush(stdout);

  while(!cfg.stop || !cfg.stop->load()){
    unique_socket s = net_accept(listener.socket.get());
    if(!s) continue;
    net_set_timeout(s.get(), cfg.timeout);
    request_log log = handle_connection(cfg, s.get());
    s.reset();
    if(log.status) printf("%s %s %d\n", log.method.c_str(), log.target.c_str(), log.status);
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

int server_main(std::span<char *const> args){
  server_config cfg;
  if(args.size() < 2){
    server_usage();
    return 2;
  }
  std::optional<std::string> dir = fs_absolute_path(args[1]);
  if(!dir){
    fprintf(stderr, "Could not find the folder %s.\n", args[1]);
    return 2;
  }
  cfg.dir = std::move(*dir);

  /* Every option takes a value: they come in pairs. */
  for(size_t i = 2; i < args.size(); i += 2){
    std::string_view opt = args[i];
    std::optional<std::string_view> value;
    if(i + 1 < args.size()) value = args[i + 1];
    std::optional<uint64_t> parsed = value ? http_parse_u64(*value) : std::nullopt;
    bool numeric = parsed.has_value();
    uint64_t number = parsed.value_or(0);
    if(opt == "--port" && numeric && number <= 65535) cfg.port = static_cast<int>(number);
    else if(opt == "--bind" && value) cfg.bind = *value;
    else if(opt == "--max-upload" && numeric) cfg.max_upload = number;
    else if(opt == "--timeout" && numeric && number > 0 && number < 3600) cfg.timeout = static_cast<int>(number);
    else if(opt == "--port-file" && value) cfg.port_file = *value;
    else{
      server_usage();
      return 2;
    }
  }
  return server_run(cfg);
}

}
