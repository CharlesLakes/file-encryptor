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
#include <time.h>
#include <unistd.h>
#include "core/append.h"
#include "core/archive.h"
#include "core/browser.h"
#include "core/fs.h"
#include "core/httpmsg.h"
#include "core/net.h"
#include "core/util.h"

#define DEFAULT_PORT       8080
#define DEFAULT_BIND       "127.0.0.1"
#define DEFAULT_MAX_UPLOAD (1ULL << 30)
#define DEFAULT_TIMEOUT    15
#define MIN_BODY_RATE      (16 * 1024)   /**< Slowest accepted upload, in bytes per second. */

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
  char id[64];
  if(!fs_file_id(fd, id, sizeof(id))) return false;
  snprintf(out, out_size, "\"%s\"", id);
  return true;
}

/**
 * @brief  Checks the If-Match header of a request against an ETag.
 * @param  req  Request.
 * @param  etag Current ETag, quoted.
 * @return True if there is no If-Match or it matches.
 */
static bool if_match_ok(const http_request *req, const char *etag){
  char value[128];
  if(!http_get_header(req, "If-Match", value, sizeof(value))) return true;
  return strcmp(value, "*") == 0 || strcmp(value, etag) == 0;
}

/* ---------------- GET and HEAD ---------------- */

/**
 * @brief  Answers GET / with the names of the archives in the folder.
 * @param  cfg       Settings.
 * @param  s         Socket.
 * @param  head_only True for HEAD.
 * @return Status code.
 */
static int serve_listing(const server_config *cfg, net_socket s, bool head_only){
  browser b;
  browser_init(&b);
  snprintf(b.dir, sizeof(b.dir), "%s", cfg->dir);
  browser_load(&b);

  size_t size = 1, len = 0;
  for(unsigned int i = 0; i < b.items.size(); i++) size += strlen(browser_get_item(&b, i)->name) + 1;
  char *text = (char *) malloc(size);
  if(!text){
    browser_free(&b);
    return send_error(s, 500, "", head_only);
  }
  for(unsigned int i = 0; i < b.items.size(); i++){
    dir_item *item = browser_get_item(&b, i);
    if(item->is_dir || !valid_archive_name(item->name)) continue;
    len += (size_t) snprintf(text + len, size - len, "%s\n", item->name);
  }
  browser_free(&b);

  send_head(s, 200, "text/plain; charset=utf-8", len, "Cache-Control: no-cache\r\n");
  if(!head_only) net_send_all(s, text, len);
  free(text);
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
static int serve_archive(const server_config *cfg, net_socket s, const http_request *req,
                         const char *name, bool head_only){
  char path[PATH_MAX], etag[80], range[128], extra[512];
  join_path(path, sizeof(path), cfg->dir, name);
  int fd = fs_open_regular(path, false);
  if(fd < 0) return send_error(s, 404, "", head_only);
  if(!make_etag(fd, etag, sizeof(etag))){
    close(fd);
    return send_error(s, 500, "", head_only);
  }
  if(!if_match_ok(req, etag)){
    close(fd);
    return send_error(s, 412, "", head_only);
  }

  uint64_t size = file_size(fd), first = 0, last = size ? size - 1 : 0;
  int status = 200;
  if(http_get_header(req, "Range", range, sizeof(range))){
    if(http_parse_range(range, size, &first, &last) != HTTP_RANGE_OK){
      close(fd);
      snprintf(extra, sizeof(extra), "Content-Range: bytes */%llu\r\n", (unsigned long long) size);
      return send_error(s, 416, extra, head_only);
    }
    status = 206;
  }
  uint64_t length = size ? last - first + 1 : 0;

  /* On Windows a file locked by the GUI cannot be read: say so before any header. */
  if(length > 0 && !read_at(fd, send_chunk, 1, first)){
    close(fd);
    return send_error(s, 503, "Retry-After: 5\r\n", head_only);
  }

  int n = snprintf(extra, sizeof(extra), "Accept-Ranges: bytes\r\nETag: %s\r\nCache-Control: no-cache\r\n", etag);
  if(status == 206)
    snprintf(extra + n, sizeof(extra) - (size_t) n, "Content-Range: bytes %llu-%llu/%llu\r\n",
             (unsigned long long) first, (unsigned long long) last, (unsigned long long) size);
  bool ok = send_head(s, status, "application/octet-stream", length, extra);
  for(uint64_t sent = 0; ok && !head_only && sent < length; ){
    size_t chunk = length - sent < sizeof(send_chunk) ? (size_t) (length - sent) : sizeof(send_chunk);
    ok = read_at(fd, send_chunk, chunk, first + sent) && net_send_all(s, send_chunk, chunk);
    sent += chunk;
  }
  close(fd);
  return status;
}

/* ---------------- POST ---------------- */

/** @brief State of body_reader(). */
typedef struct{
  net_socket s;                    /**< Connection. */
  const char *pending;             /**< Body bytes already received with the head. */
  size_t pending_len;              /**< Number of pending bytes. */
  time_t deadline;                 /**< Time after which the upload is abandoned. */
}body_ctx;

/** @brief append_reader over the connection, starting with the pending bytes. */
static bool body_reader(void *ctx, void *buf, size_t len){
  body_ctx *b = (body_ctx *) ctx;
  unsigned char *out = (unsigned char *) buf;
  size_t take = len < b->pending_len ? len : b->pending_len;
  memcpy(out, b->pending, take);
  b->pending += take;
  b->pending_len -= take;
  for(size_t got = take; got < len; ){
    if(time(NULL) > b->deadline) return false;
    long n = net_recv(b->s, out + got, len - got);
    if(n <= 0) return false;
    got += (size_t) n;
  }
  return true;
}

/**
 * @brief  Reads the claim of a signed append from the request headers.
 * @param  req Request.
 * @param  c   Output: the claim.
 * @return True if every header is present and well formed.
 */
static bool read_claim(const http_request *req, append_claim *c){
  char value[256];
  memset(c, 0, sizeof(*c));
  c->content_length = req->content_length;
  return http_get_header(req, "X-Crls-Base", value, sizeof(value)) &&
         http_parse_u64(value, strlen(value), &c->base) &&
         http_get_header(req, "X-Crls-Index-Length", value, sizeof(value)) &&
         http_parse_u64(value, strlen(value), &c->index_length) &&
         http_get_header(req, "X-Crls-Index-Nonce", value, sizeof(value)) &&
         http_hex_decode(value, c->index_nonce, NONCE_BYTES) &&
         http_get_header(req, "X-Crls-Body-Hash", value, sizeof(value)) &&
         http_hex_decode(value, c->body_hash, BODY_HASH_BYTES) &&
         http_get_header(req, "X-Crls-Sig", value, sizeof(value)) &&
         http_hex_decode(value, c->signature, SIGN_BYTES);
}

/**
 * @brief  Maps the result of an append step to an HTTP status code.
 * @param  r Result.
 * @return Status code.
 */
static int append_status(append_result r){
  switch(r){
    case APPEND_OK:          return 204;
    case APPEND_BAD_REQUEST: return 400;
    case APPEND_FORBIDDEN:   return 403;
    case APPEND_CONFLICT:    return 409;
    case APPEND_TOO_LARGE:   return 413;
    case APPEND_UNSUPPORTED: return 422;
    case APPEND_IO:          return 500;
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
static int serve_append(const server_config *cfg, net_socket s, const http_request *req, const char *name,
                        const char *pending, size_t pending_len){
  append_claim claim;
  if(!req->has_length) return send_error(s, 411, "", false);
  if(!read_claim(req, &claim)) return send_error(s, 400, "", false);
  if(claim.content_length > cfg->max_upload) return send_error(s, 413, "", false);

  char path[PATH_MAX], etag[80];
  join_path(path, sizeof(path), cfg->dir, name);
  int fd = fs_open_regular(path, true);
  if(fd < 0) return send_error(s, 404, "", false);
  if(!make_etag(fd, etag, sizeof(etag)) || !if_match_ok(req, etag)){
    close(fd);
    return send_error(s, 412, "", false);
  }
  if(!fs_lock(fd)){
    close(fd);
    return send_error(s, 423, "", false);
  }

  append_state st;
  append_result r = append_load_state(fd, &st);
  if(r == APPEND_OK) r = append_check(&st, &claim, cfg->max_upload);
  if(r == APPEND_OK){
    char expect[64];
    if(http_get_header(req, "Expect", expect, sizeof(expect)) && strcmp(expect, "100-continue") == 0)
      net_send_all(s, "HTTP/1.1 100 Continue\r\n\r\n", 25);
    body_ctx ctx = {s, pending, pending_len,
                    time(NULL) + cfg->timeout + (time_t) (claim.content_length / MIN_BODY_RATE)};
    r = append_commit(fd, &st, &claim, body_reader, &ctx);
  }
  close(fd);
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
static int handle_connection(const server_config *cfg, net_socket s, char *method, char *target){
  static char head[HTTP_MAX_HEAD];
  http_request req;
  http_parse_result parsed = HTTP_PARSE_INCOMPLETE;
  size_t len = 0;
  time_t deadline = time(NULL) + cfg->timeout;

  while(parsed == HTTP_PARSE_INCOMPLETE){
    if(len == sizeof(head)){
      parsed = HTTP_PARSE_TOO_LARGE;
      break;
    }
    long n = net_recv(s, head + len, sizeof(head) - len);
    if(n <= 0 || time(NULL) > deadline) return len ? send_error(s, 408, "", false) : 0;
    len += (size_t) n;
    parsed = http_parse_request(head, len, &req);
  }
  if(parsed == HTTP_PARSE_TOO_LARGE) return send_error(s, 431, "", false);
  if(parsed == HTTP_PARSE_UNSUPPORTED) return send_error(s, 501, "", false);
  if(parsed != HTTP_PARSE_OK) return send_error(s, 400, "", false);
  snprintf(method, 16, "%s", req.method);
  snprintf(target, 256, "%.255s", req.target);   /* Truncated for the log. */

  bool get = strcmp(req.method, "GET") == 0, head_only = strcmp(req.method, "HEAD") == 0;
  bool post = strcmp(req.method, "POST") == 0;
  if(!get && !head_only && !post) return send_error(s, 405, "Allow: GET, HEAD, POST\r\n", false);

  char name[1024];
  if(!http_decode_path(req.target, name, sizeof(name))) return send_error(s, 400, "", head_only);
  if(name[0] == '\0'){
    if(post) return send_error(s, 405, "Allow: GET, HEAD\r\n", false);
    return serve_listing(cfg, s, head_only);
  }
  if(!valid_archive_name(name)) return send_error(s, 404, "", head_only);
  if(post) return serve_append(cfg, s, &req, name, head + req.head_length, len - req.head_length);
  return serve_archive(cfg, s, &req, name, head_only);
}

int server_run(const server_config *cfg){
  if(!net_init()){
    fprintf(stderr, "Could not initialize the network.\n");
    return 1;
  }
  int port = 0;
  net_socket listener = net_listen(cfg->bind, cfg->port, &port);
  if(listener == NET_INVALID){
    fprintf(stderr, "Could not listen on %s port %d.\n", cfg->bind, cfg->port);
    return 1;
  }
  if(cfg->port_file){
    FILE *f = fopen(cfg->port_file, "w");
    if(f){
      fprintf(f, "%d\n", port);
      fclose(f);
    }
  }
  printf("Serving %s on http://%s:%d/\n", cfg->dir, cfg->bind, port);
  fflush(stdout);

  while(!cfg->stop || !*cfg->stop){
    net_socket s = net_accept(listener);
    if(s == NET_INVALID) continue;
    net_set_timeout(s, cfg->timeout);
    char method[16] = "-", target[256] = "-";
    int status = handle_connection(cfg, s, method, target);
    net_close(s);
    if(status) printf("%s %s %d\n", method, target, status);
    fflush(stdout);
  }
  net_close(listener);
  return 0;
}

/**
 * @brief Prints the usage of the server options.
 */
static void server_usage(void){
  fprintf(stderr, "usage: file-encryptor --server <folder> [--port N] [--bind ADDRESS]\n"
                  "                      [--max-upload BYTES] [--timeout SECONDS] [--port-file PATH]\n"
                  "defaults: --port %d --bind %s --max-upload %llu --timeout %d\n",
          DEFAULT_PORT, DEFAULT_BIND, (unsigned long long) DEFAULT_MAX_UPLOAD, DEFAULT_TIMEOUT);
}

int server_main(int argc, char **argv){
  static char dir[PATH_MAX];
  server_config cfg = {NULL, DEFAULT_BIND, DEFAULT_PORT, DEFAULT_MAX_UPLOAD, DEFAULT_TIMEOUT, NULL, NULL};
  if(argc < 2){
    server_usage();
    return 2;
  }
  if(!fs_absolute_path(argv[1], dir)){
    fprintf(stderr, "Could not find the folder %s.\n", argv[1]);
    return 2;
  }
  cfg.dir = dir;

  /* Every option takes a value: they come in pairs. */
  for(int i = 2; i < argc; i += 2){
    const char *opt = argv[i], *value = i + 1 < argc ? argv[i + 1] : NULL;
    uint64_t number;
    bool numeric = value && http_parse_u64(value, strlen(value), &number);
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
  return server_run(&cfg);
}
