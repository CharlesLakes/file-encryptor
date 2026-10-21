/**
 * @file    test_server.cpp
 * @brief   End-to-end tests of the HTTP server (server/server.c) over real
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
#include <unistd.h>
#include "core/append.h"
#include "core/archive.h"
#include "core/fs.h"
#include "core/httpmsg.h"
#include "core/net.h"
#include "core/util.h"
#include "server_fixture.h"
#include "test_util.h"

#define PASSWORD "server-password"

static int port;

/** @brief A response read by request(). */
typedef struct{
  int status;                          /**< Status code, or 0 if none was received. */
  char head[4096];                     /**< Status line and headers. */
  unsigned char *body;                 /**< Body (free with free()). */
  size_t body_len;                     /**< Length of body. */
}response;

/**
 * @brief  Sends a raw request and reads the whole response.
 * @param  head     Request head, ending with the blank line.
 * @param  body     Body to send after the head, or NULL.
 * @param  body_len Length of body.
 * @param  r        Output: the response.
 * @return True if a response with a status line was received.
 */
static bool request(const char *head, const void *body, size_t body_len, response *r){
  memset(r, 0, sizeof(*r));
  net_socket s = net_connect("127.0.0.1", port);
  if(s == NET_INVALID) return false;
  net_set_timeout(s, 10);
  bool ok = net_send_all(s, head, strlen(head)) && (!body || net_send_all(s, body, body_len));

  size_t cap = 1 << 16, len = 0;
  unsigned char *buf = (unsigned char *) malloc(cap);
  long n;
  while(ok && (n = net_recv(s, buf + len, cap - len)) > 0){
    len += (size_t) n;
    if(len == cap) buf = (unsigned char *) realloc(buf, cap *= 2);
  }
  net_close(s);

  unsigned char *end = NULL;
  for(size_t i = 0; i + 3 < len; i++)
    if(memcmp(buf + i, "\r\n\r\n", 4) == 0){
      end = buf + i + 4;
      break;
    }
  /* Skip an interim 100 Continue response. */
  if(end && len > 12 && memcmp(buf + 9, "100", 3) == 0){
    size_t skip = (size_t) (end - buf);
    memmove(buf, end, len - skip);
    len -= skip;
    end = NULL;
    for(size_t i = 0; i + 3 < len; i++)
      if(memcmp(buf + i, "\r\n\r\n", 4) == 0){
        end = buf + i + 4;
        break;
      }
  }
  if(!end || len < 12){
    free(buf);
    return false;
  }
  size_t head_len = (size_t) (end - buf);
  memcpy(r->head, buf, head_len < sizeof(r->head) - 1 ? head_len : sizeof(r->head) - 1);
  r->status = atoi((const char *) buf + 9);
  r->body_len = len - head_len;
  r->body = (unsigned char *) malloc(r->body_len + 1);
  memcpy(r->body, end, r->body_len);
  r->body[r->body_len] = 0;
  free(buf);
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
  char nonce[2*NONCE_BYTES + 1], hash[2*BODY_HASH_BYTES + 1], sig[2*SIGN_BYTES + 1];
  http_hex_encode(up->claim.index_nonce, NONCE_BYTES, nonce);
  http_hex_encode(up->claim.body_hash, BODY_HASH_BYTES, hash);
  http_hex_encode(up->claim.signature, SIGN_BYTES, sig);
  snprintf(out, 1024, "POST %s HTTP/1.1\r\nHost: test\r\nContent-Length: %llu\r\nX-Crls-Base: %llu\r\n"
                      "X-Crls-Index-Nonce: %s\r\nX-Crls-Index-Length: %llu\r\nX-Crls-Body-Hash: %s\r\n"
                      "X-Crls-Sig: %s\r\n\r\n",
           target, (unsigned long long) up->claim.content_length, (unsigned long long) up->claim.base,
           nonce, (unsigned long long) up->claim.index_length, hash, sig);
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
  unsigned char *body = (unsigned char *) malloc((size_t) up->claim.content_length + 1);
  read_at(up->body_fd, body, (size_t) up->claim.content_length, 0);
  response r;
  bool ok = request(head, body, (size_t) send, &r);
  free(body);
  free(r.body);
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
  archive_init(&a);
  char path[PATH_MAX];
  path_in(path, file);
  bool ok = archive_open(&a, crls, PASSWORD) && append_prepare(&a, path, up);
  archive_free(&a);
  return ok;
}

int main(int argc, char **argv){
  if(argc < 2){
    printf("usage: %s <fixtures folder>\n", argv[0]);
    return 2;
  }
  CHECK(archive_crypto_init());
  CHECK(make_temp_dir());
  CHECK(net_init());

  /* The served folder: a version 3 archive, the version 2 fixture and a file
     that must not be served. */
  char served[PATH_MAX], crls[PATH_MAX], file[PATH_MAX], source[PATH_MAX], v2[PATH_MAX];
  path_in(served, "served");
  CHECK(make_dir(served) == 0);
  join_path(crls, sizeof(crls), served, "remote.crls");
  join_path(v2, sizeof(v2), served, "old.crls");
  join_path(source, sizeof(source), argv[1], "v2.crls");
  CHECK(copy_file(source, v2));
  make_file("served/notes.txt", 10, 1);
  make_file("first.bin", 3*CHUNK + 1, 21);
  make_file("second.bin", 1234, 22);
  make_file("third.bin", 999, 23);

  archive a;
  archive_init(&a);
  CHECK(archive_create_params(&a, crls, PASSWORD, 1, 8u << 20));
  path_in(file, "first.bin");
  CHECK(archive_add_file(&a, file));
  archive_close(&a);

  port = start_test_server(served);
  CHECK(port > 0);

  response r;
  /* Listing: archives only. */
  CHECK(get("GET", "/", "", &r) == 200);
  CHECK(strcmp((char *) r.body, "old.crls\nremote.crls\n") == 0);
  free(r.body);

  /* Whole file, a range, and HEAD. */
  int fd = open(crls, O_RDONLY | O_BINARY);
  uint64_t size = file_size(fd);
  unsigned char *expected = (unsigned char *) malloc((size_t) size);
  CHECK(read_at(fd, expected, (size_t) size, 0));
  close(fd);
  CHECK(get("GET", "/remote.crls", "", &r) == 200);
  CHECK(r.body_len == size && memcmp(r.body, expected, (size_t) size) == 0);
  free(r.body);
  CHECK(get("GET", "/remote.crls", "Range: bytes=10-99\r\n", &r) == 206);
  CHECK(r.body_len == 90 && memcmp(r.body, expected + 10, 90) == 0);
  CHECK(strstr(r.head, "Content-Range: bytes 10-99/") != NULL);
  char etag[128] = "";
  const char *e = strstr(r.head, "ETag: ");
  if(e) sscanf(e + 6, "%127[^\r]", etag);
  CHECK(etag[0] == '"');
  free(r.body);
  CHECK(get("HEAD", "/remote.crls", "", &r) == 200 && r.body_len == 0);
  free(r.body);
  free(expected);

  /* Errors. */
  CHECK(get("GET", "/remote.crls", "Range: bytes=0-1,5-6\r\n", &r) == 416);
  free(r.body);
  CHECK(get("GET", "/remote.crls", "If-Match: \"other\"\r\n", &r) == 412);
  free(r.body);
  CHECK(get("GET", "/notes.txt", "", &r) == 404);
  free(r.body);
  CHECK(get("GET", "/../served/remote.crls", "", &r) == 404);
  free(r.body);
  CHECK(get("GET", "/%2e%2e%2fserved%2fremote.crls", "", &r) == 404);
  free(r.body);
  CHECK(get("DELETE", "/remote.crls", "", &r) == 405);
  free(r.body);
  CHECK(request("POST /remote.crls HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n", NULL, 0, &r) && r.status == 501);
  free(r.body);
  CHECK(request("POST /remote.crls HTTP/1.1\r\nHost: test\r\n\r\n", NULL, 0, &r) && r.status == 411);
  free(r.body);

  /* A valid upload. Its body is only the new entry and the new index. */
  append_upload up;
  CHECK(prepare(crls, "second.bin", &up));
  CHECK(up.claim.content_length == up.entry.stored + up.claim.index_length);
  CHECK(post("/remote.crls", &up, up.claim.content_length) == 204);
  append_discard(&up);

  /* A signature made without the password. */
  archive_init(&a);
  CHECK(archive_open(&a, crls, PASSWORD));
  path_in(file, "third.bin");
  memset(a.sign_sk, 7, sizeof(a.sign_sk));
  CHECK(append_prepare(&a, file, &up));
  archive_free(&a);
  CHECK(post("/remote.crls", &up, up.claim.content_length) == 403);
  append_discard(&up);

  /* A client that disconnects in the middle of the body changes nothing. */
  fd = open(crls, O_RDONLY | O_BINARY);
  size = file_size(fd);
  close(fd);
  CHECK(prepare(crls, "third.bin", &up));
  post("/remote.crls", &up, up.claim.content_length / 2);
  /* The server may still be finishing that connection; this request waits for it. */
  CHECK(get("HEAD", "/", "", &r) == 200);
  free(r.body);
  fd = open(crls, O_RDONLY | O_BINARY);
  CHECK(file_size(fd) == size);
  close(fd);

  /* While the archive is locked (open in the GUI), uploads wait: 423. */
  archive_init(&a);
  CHECK(archive_open(&a, crls, PASSWORD));
  CHECK(post("/remote.crls", &up, up.claim.content_length) == 423);
  archive_close(&a);
  CHECK(post("/remote.crls", &up, up.claim.content_length) == 204);
  append_discard(&up);

  /* Version 2 archives cannot receive uploads. */
  CHECK(prepare(crls, "third.bin", &up) == false);   /* Already uploaded. */
  make_file("fourth.bin", 10, 24);
  CHECK(prepare(crls, "fourth.bin", &up));
  CHECK(post("/old.crls", &up, up.claim.content_length) == 422);
  append_discard(&up);

  /* The archive now holds the three files, readable with the password. */
  archive_init(&a);
  CHECK(archive_open(&a, crls, PASSWORD));
  CHECK(a.entries.size() == 3);
  char out[PATH_MAX];
  path_in(out, "out");
  CHECK(make_dir(out) == 0);
  const char *names[] = {"first.bin", "second.bin", "third.bin"};
  for(unsigned int i = 0; i < 3 && i < a.entries.size(); i++){
    char original[PATH_MAX], extracted[PATH_MAX];
    CHECK(strcmp(archive_get_entry(&a, i)->name, names[i]) == 0);
    CHECK(archive_extract_entry(&a, i, out) == 1);
    path_in(original, names[i]);
    join_path(extracted, sizeof(extracted), out, names[i]);
    CHECK(same_files(original, extracted));
  }
  archive_free(&a);

  stop_test_server();

  remove_temp_dir();
  return test_summary();
}
