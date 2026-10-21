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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <unistd.h>
#include "core/append.h"
#include "core/archive.h"
#include "core/fs.h"
#include "core/httpmsg.h"
#include "core/net.h"
#include "core/util.h"
#include "server_fixture.h"
#include "test_util.h"

using namespace crls;

#define PASSWORD "server-password"

static int port;

/** @brief A response read by request(). */
struct response{
  int status = 0;                      /**< Status code, or 0 if none was received. */
  std::string head;                    /**< Status line and headers. */
  std::string body;                    /**< Body. */
};

/**
 * @brief  Sends a raw request and reads the whole response.
 * @param  head     Request head, ending with the blank line.
 * @param  body     Body to send after the head, or NULL.
 * @param  body_len Length of body.
 * @param  r        Output: the response.
 * @return True if a response with a status line was received.
 */
static bool request(const char *head, const void *body, size_t body_len, response *r){
  *r = response();
  unique_socket s = net_connect("127.0.0.1", port);
  if(!s) return false;
  net_set_timeout(s.get(), 10);
  bool ok = net_send_all(s.get(), std::string_view(head)) &&
            (!body || net_send_all(s.get(), {static_cast<const unsigned char *>(body), body_len}));

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
  if(end == std::string::npos || received.size() < 12) return false;
  r->head = received.substr(0, end + 4);
  r->body = received.substr(end + 4);
  r->status = atoi(received.c_str() + 9);
  return true;
}

/**
 * @brief  Sends a GET or HEAD request and returns the status code.
 * @param  method "GET" or "HEAD".
 * @param  target Request target.
 * @param  extra  Additional header lines (may be "").
 * @param  r      Output: the response (free r->body).
 * @return Status code.
 */
static int get(const char *method, const char *target, const char *extra, response *r){
  char head[1024];
  snprintf(head, sizeof(head), "%s %s HTTP/1.1\r\nHost: test\r\n%s\r\n", method, target, extra);
  return request(head, NULL, 0, r) ? r->status : 0;
}

/**
 * @brief  Builds the head of a signed append request for an upload.
 * @param  target Request target.
 * @param  up     Prepared upload.
 * @param  out    Destination buffer of 1024 bytes.
 */
static void post_head(const char *target, const append_upload *up, char *out){
  std::string nonce = http_hex_encode(up->claim.index_nonce), hash = http_hex_encode(up->claim.body_hash);
  std::string sig = http_hex_encode(up->claim.signature);
  snprintf(out, 1024, "POST %s HTTP/1.1\r\nHost: test\r\nContent-Length: %llu\r\nX-Crls-Base: %llu\r\n"
                      "X-Crls-Index-Nonce: %s\r\nX-Crls-Index-Length: %llu\r\nX-Crls-Body-Hash: %s\r\n"
                      "X-Crls-Sig: %s\r\n\r\n",
           target, (unsigned long long) up->claim.content_length, (unsigned long long) up->claim.base,
           nonce.c_str(), (unsigned long long) up->claim.index_length, hash.c_str(), sig.c_str());
}

/**
 * @brief  Sends a prepared upload and returns the status code.
 * @param  target Request target.
 * @param  up     Prepared upload.
 * @param  send   Number of body bytes to send (less than the full body to
 *                simulate a client that disconnects).
 * @return Status code, or 0 if no response arrived.
 */
static int post(const char *target, const append_upload *up, uint64_t send){
  char head[1024];
  post_head(target, up, head);
  std::vector<unsigned char> body(static_cast<size_t>(up->claim.content_length));
  CHECK(read_at(up->body_fd(), body, 0));
  response r;
  bool ok = request(head, body.data(), static_cast<size_t>(send), &r);
  return ok ? r.status : 0;
}

/**
 * @brief  Opens an archive and prepares an upload, then closes the archive.
 * @param  crls Path of the archive.
 * @param  file Name of the file to upload, inside the test folder.
 * @param  up   Output: the prepared upload.
 * @return True on success.
 */
static bool prepare(const char *crls, const char *file, append_upload *up){
  archive a;
  char path[PATH_MAX];
  path_in(path, file);
  bool ok = a.open(crls, PASSWORD) && up->prepare(a, path);
  a.close();
  return ok;
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
  char served[PATH_MAX], crls[PATH_MAX], file[PATH_MAX], source[PATH_MAX], v2[PATH_MAX];
  path_in(served, "served");
  CHECK_EQ(make_dir(served), 0);
  snprintf(crls, sizeof(crls), "%s", join_path(served, "remote.crls").c_str());
  snprintf(v2, sizeof(v2), "%s", join_path(served, "old.crls").c_str());
  snprintf(source, sizeof(source), "%s", join_path(argv[1], "v2.crls").c_str());
  CHECK(copy_file(source, v2));
  make_file("served/notes.txt", 10, 1);
  make_file("first.bin", 3*CHUNK + 1, 21);
  make_file("second.bin", 1234, 22);
  make_file("third.bin", 999, 23);

  archive a;
  CHECK(a.create(crls, PASSWORD, 1, 8u << 20));
  path_in(file, "first.bin");
  CHECK(a.add_file(file));
  a.close();

  test_server server(served);
  port = server.port();
  CHECK(port > 0);

  response r;
  /* Listing: archives only. */
  CHECK(get("GET", "/", "", &r) == 200);
  CHECK_EQ(r.body, "old.crls\nremote.crls\n");

  /* Whole file, a range, and HEAD. */
  int fd = open(crls, O_RDONLY | O_BINARY);
  uint64_t size = file_size(fd);
  std::vector<unsigned char> expected(static_cast<size_t>(size));
  CHECK(read_at(fd, expected, 0));
  close(fd);
  CHECK(get("GET", "/remote.crls", "", &r) == 200);
  CHECK(r.body.size() == size && memcmp(r.body.data(), expected.data(), expected.size()) == 0);
  CHECK(get("GET", "/remote.crls", "Range: bytes=10-99\r\n", &r) == 206);
  CHECK(r.body.size() == 90 && memcmp(r.body.data(), expected.data() + 10, 90) == 0);
  CHECK(strstr(r.head.c_str(), "Content-Range: bytes 10-99/") != NULL);
  char etag[128] = "";
  const char *e = strstr(r.head.c_str(), "ETag: ");
  if(e) sscanf(e + 6, "%127[^\r]", etag);
  CHECK(etag[0] == '"');
  CHECK(get("HEAD", "/remote.crls", "", &r) == 200 && r.body.size() == 0);

  /* Errors. */
  CHECK(get("GET", "/remote.crls", "Range: bytes=0-1,5-6\r\n", &r) == 416);
  CHECK(get("GET", "/remote.crls", "If-Match: \"other\"\r\n", &r) == 412);
  CHECK(get("GET", "/notes.txt", "", &r) == 404);
  CHECK(get("GET", "/../served/remote.crls", "", &r) == 404);
  CHECK(get("GET", "/%2e%2e%2fserved%2fremote.crls", "", &r) == 404);
  CHECK(get("DELETE", "/remote.crls", "", &r) == 405);
  CHECK(request("POST /remote.crls HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", NULL, 0, &r) && r.status == 501);
  CHECK(request("POST /remote.crls HTTP/1.1\r\nHost: test\r\n\r\n", NULL, 0, &r) && r.status == 411);

  /* A valid upload. Its body is only the new entry and the new index. */
  append_upload up;
  CHECK(prepare(crls, "second.bin", &up));
  CHECK(up.claim.content_length == up.entry().stored + up.claim.index_length);
  CHECK(post("/remote.crls", &up, up.claim.content_length) == 204);

  /* A signature made without the password. */
  CHECK(a.open(crls, PASSWORD));
  path_in(file, "third.bin");
  memset(a.signing_key().data(), 7, SIGN_SK_BYTES);
  CHECK(up.prepare(a, file));
  a.close();
  CHECK(post("/remote.crls", &up, up.claim.content_length) == 403);

  /* A client that disconnects in the middle of the body changes nothing. */
  fd = open(crls, O_RDONLY | O_BINARY);
  size = file_size(fd);
  close(fd);
  CHECK(prepare(crls, "third.bin", &up));
  post("/remote.crls", &up, up.claim.content_length / 2);
  /* The server may still be finishing that connection; this request waits for it. */
  CHECK(get("HEAD", "/", "", &r) == 200);
  fd = open(crls, O_RDONLY | O_BINARY);
  CHECK_EQ(file_size(fd), size);
  close(fd);

  /* While the archive is locked (open in the GUI), uploads wait: 423. */
  CHECK(a.open(crls, PASSWORD));
  CHECK(post("/remote.crls", &up, up.claim.content_length) == 423);
  a.close();
  CHECK(post("/remote.crls", &up, up.claim.content_length) == 204);

  /* Version 2 archives cannot receive uploads. */
  CHECK(prepare(crls, "third.bin", &up) == false);   /* Already uploaded. */
  make_file("fourth.bin", 10, 24);
  CHECK(prepare(crls, "fourth.bin", &up));
  CHECK(post("/old.crls", &up, up.claim.content_length) == 422);

  /* The archive now holds the three files, readable with the password. */
  CHECK(a.open(crls, PASSWORD));
  CHECK_EQ(a.entries().size(), 3);
  char out[PATH_MAX];
  path_in(out, "out");
  CHECK_EQ(make_dir(out), 0);
  const char *names[] = {"first.bin", "second.bin", "third.bin"};
  for(unsigned int i = 0; i < 3 && i < a.entries().size(); i++){
    char original[PATH_MAX], extracted[PATH_MAX];
    CHECK(a.entries()[i].name == names[i]);
    CHECK(a.extract(i, out) == extract_result::extracted);
    path_in(original, names[i]);
    snprintf(extracted, sizeof(extracted), "%s", join_path(out, names[i]).c_str());
    CHECK(same_files(original, extracted));
  }
  a.close();


  return test_summary();
}
