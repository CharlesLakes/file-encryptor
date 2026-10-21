/**
 * @file    test_server.cpp
 * @brief   End-to-end tests of the HTTP server (server/server.cpp) over real
 *          sockets on 127.0.0.1.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * The server runs in a second thread of this process (see server_fixture.h).
 * The only argument is the folder holding tests/fixtures/v2.crls.
 */

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include "core/append.h"
#include "core/archive.h"
#include "core/fs.h"
#include "core/httpmsg.h"
#include "core/net.h"
#include "core/util.h"
#include "server_fixture.h"
#include "test_util.h"

using namespace crls;

inline constexpr char PASSWORD[] = "server-password";

/** @brief A response read by request(). */
struct response{
  int status = 0;                      /**< Status code. */
  std::string head;                    /**< Status line and headers. */
  std::string body;                    /**< Body. */

  /**
   * @brief  Returns the value of a header (case-sensitive, enough for tests).
   * @param  name Header name, e.g. "ETag".
   * @return The value, or an empty view.
   */
  std::string_view header(std::string_view name) const{
    std::string_view h = head;
    std::size_t start = h.find(std::string(name) + ": ");
    if(start == std::string_view::npos) return {};
    h.remove_prefix(start + name.size() + 2);
    return h.substr(0, h.find('\r'));
  }
};

/**
 * @brief  Sends a raw request and reads the whole response.
 * @param  port Port of the server.
 * @param  head Request head, ending with the blank line.
 * @param  body Body to send after the head.
 * @return The response, or std::nullopt if none with a status line arrived.
 */
static std::optional<response> request(int port, std::string_view head, std::span<const unsigned char> body = {}){
  unique_socket s = net_connect("127.0.0.1", port);
  if(!s) return std::nullopt;
  net_set_timeout(s.get(), 10);
  bool ok = net_send_all(s.get(), head) && net_send_all(s.get(), body);

  std::string received;
  std::vector<char> chunk(1 << 16);
  std::optional<size_t> n;
  while(ok && (n = net_recv(s.get(), std::span(chunk))) && *n > 0) received.append(chunk.data(), *n);
  s.reset();

  size_t end = received.find("\r\n\r\n");
  /* Skip an interim 100 Continue response. */
  if(end != std::string::npos && received.compare(9, 3, "100") == 0){
    received.erase(0, end + 4);
    end = received.find("\r\n\r\n");
  }
  if(end == std::string::npos || received.size() < 12) return std::nullopt;
  response r;
  r.head = received.substr(0, end + 4);
  r.body = received.substr(end + 4);
  std::from_chars(received.data() + 9, received.data() + 12, r.status);
  return r;
}

/**
 * @brief  Sends a GET or HEAD request.
 * @param  port   Port of the server.
 * @param  method "GET" or "HEAD".
 * @param  target Request target.
 * @param  extra  Additional header lines (may be empty).
 * @return The response; its status is 0 if none arrived.
 */
static response get(int port, std::string_view method, std::string_view target, std::string_view extra = {}){
  std::string head = std::string(method) + " " + std::string(target) + " HTTP/1.1\r\nHost: test\r\n" +
                     std::string(extra) + "\r\n";
  return request(port, head).value_or(response());
}

/**
 * @brief  Builds the head of a signed append request for an upload.
 * @param  target Request target.
 * @param  up     Prepared upload.
 * @return The head, ending with the blank line.
 */
static std::string post_head(std::string_view target, const append_upload &up){
  return "POST " + std::string(target) + " HTTP/1.1\r\nHost: test\r\n"
         "Content-Length: " + std::to_string(up.claim.content_length) + "\r\n"
         "X-Crls-Base: " + std::to_string(up.claim.base) + "\r\n"
         "X-Crls-Index-Nonce: " + http_hex_encode(up.claim.index_nonce) + "\r\n"
         "X-Crls-Index-Length: " + std::to_string(up.claim.index_length) + "\r\n"
         "X-Crls-Body-Hash: " + http_hex_encode(up.claim.body_hash) + "\r\n"
         "X-Crls-Sig: " + http_hex_encode(up.claim.signature) + "\r\n\r\n";
}

/**
 * @brief  Sends a prepared upload and returns the status code.
 * @param  port   Port of the server.
 * @param  target Request target.
 * @param  up     Prepared upload.
 * @param  send   Number of body bytes to send (less than the full body to
 *                simulate a client that disconnects).
 * @return Status code, or 0 if no response arrived.
 */
static int post(int port, std::string_view target, const append_upload &up, uint64_t send){
  std::vector<unsigned char> body(static_cast<size_t>(up.claim.content_length));
  CHECK(read_at(up.body_fd(), body, 0));
  std::optional<response> r = request(port, post_head(target, up), std::span(body).first(static_cast<size_t>(send)));
  return r ? r->status : 0;
}

int main(int argc, char **argv){
  if(argc < 2){
    printf("usage: %s <fixtures folder>\n", argv[0]);
    return 2;
  }
  CHECK(archive_crypto_init());
  temp_dir scratch;
  CHECK(static_cast<bool>(scratch));
  CHECK(net_init());

  /* The served folder: a version 3 archive, the version 2 fixture and a file
     that must not be served. */
  std::string served = path_in("served");
  CHECK(make_dir(served));
  std::string crls = join_path(served, "remote.crls");
  CHECK(copy_file(join_path(argv[1], "v2.crls"), join_path(served, "old.crls")));
  make_file("served/notes.txt", 10, 1);
  make_file("first.bin", 3*CHUNK + 1, 21);
  make_file("second.bin", 1234, 22);
  make_file("third.bin", 999, 23);

  archive a;
  CHECK(a.create(crls, PASSWORD, 1, 8u << 20));
  CHECK(a.add_file(path_in("first.bin")));
  a.close();

  test_server server(served);
  int port = server.port();
  CHECK(port > 0);

  /* Listing: archives only. */
  response r = get(port, "GET", "/");
  CHECK_EQ(r.status, 200);
  CHECK_EQ(r.body, "old.crls\nremote.crls\n");

  /* Whole file, a range, and HEAD. */
  std::optional<std::string> expected = read_file(crls);
  CHECK(expected.has_value());
  r = get(port, "GET", "/remote.crls");
  CHECK(r.status == 200 && expected && r.body == *expected);
  r = get(port, "GET", "/remote.crls", "Range: bytes=10-99\r\n");
  CHECK_EQ(r.status, 206);
  CHECK(expected && r.body == expected->substr(10, 90));
  CHECK(r.header("Content-Range").starts_with("bytes 10-99/"));
  CHECK(r.header("ETag").starts_with('"'));
  r = get(port, "HEAD", "/remote.crls");
  CHECK(r.status == 200 && r.body.empty());

  /* Errors. */
  CHECK_EQ(get(port, "GET", "/remote.crls", "Range: bytes=0-1,5-6\r\n").status, 416);
  CHECK_EQ(get(port, "GET", "/remote.crls", "If-Match: \"other\"\r\n").status, 412);
  CHECK_EQ(get(port, "GET", "/notes.txt").status, 404);
  CHECK_EQ(get(port, "GET", "/../served/remote.crls").status, 404);
  CHECK_EQ(get(port, "GET", "/%2e%2e%2fserved%2fremote.crls").status, 404);
  CHECK_EQ(get(port, "DELETE", "/remote.crls").status, 405);
  CHECK_EQ(request(port, "POST /remote.crls HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n").value_or(response()).status, 501);
  CHECK_EQ(request(port, "POST /remote.crls HTTP/1.1\r\nHost: test\r\n\r\n").value_or(response()).status, 411);

  /* A valid upload. Its body is only the new entry and the new index. */
  append_upload up;
  CHECK(prepare_upload(crls, PASSWORD, "second.bin", up));
  CHECK(up.claim.content_length == up.entry().stored + up.claim.index_length);
  CHECK_EQ(post(port, "/remote.crls", up, up.claim.content_length), 204);

  /* A signature made without the password. */
  CHECK(a.open(crls, PASSWORD));
  std::ranges::fill(a.signing_key(), 7);
  CHECK(up.prepare(a, path_in("third.bin")));
  a.close();
  CHECK_EQ(post(port, "/remote.crls", up, up.claim.content_length), 403);

  /* A client that disconnects in the middle of the body changes nothing. */
  uint64_t size = size_of(crls);
  CHECK(prepare_upload(crls, PASSWORD, "third.bin", up));
  post(port, "/remote.crls", up, up.claim.content_length / 2);
  /* The server may still be finishing that connection; this request waits for it. */
  CHECK_EQ(get(port, "HEAD", "/").status, 200);
  CHECK_EQ(size_of(crls), size);

  /* While the archive is locked (open in the GUI), uploads wait: 423. */
  CHECK(a.open(crls, PASSWORD));
  CHECK_EQ(post(port, "/remote.crls", up, up.claim.content_length), 423);
  a.close();
  CHECK_EQ(post(port, "/remote.crls", up, up.claim.content_length), 204);

  /* Version 2 archives cannot receive uploads. */
  CHECK(!prepare_upload(crls, PASSWORD, "third.bin", up));   /* Already uploaded. */
  make_file("fourth.bin", 10, 24);
  CHECK(prepare_upload(crls, PASSWORD, "fourth.bin", up));
  CHECK_EQ(post(port, "/old.crls", up, up.claim.content_length), 422);

  /* The archive now holds the three files, readable with the password. */
  CHECK(a.open(crls, PASSWORD));
  constexpr std::array<std::string_view, 3> names = {"first.bin", "second.bin", "third.bin"};
  check_extract(a, "out", names);
  a.close();

  return test_summary();
}
