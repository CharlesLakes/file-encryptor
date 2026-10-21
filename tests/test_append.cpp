/**
 * @file    test_append.cpp
 * @brief   Tests of signed appends (core/append.cpp), with the client and the
 *          server side in the same process and no network.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * The only argument is the folder holding tests/fixtures/v2.crls.
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "core/append.h"
#include "core/archive.h"
#include "core/fs.h"
#include "core/util.h"
#include "deps/sodium.h"
#include "test_util.h"

using namespace crls;

#define PASSWORD "upload-password"
#define MAX_BODY (64ULL << 20)

/**
 * @brief  Runs the server side of an append on the archive at crls.
 * @param  crls Path of the archive (must not be open elsewhere).
 * @param  up   Upload prepared by a client.
 * @return Result of the first step that did not succeed, or append_result::ok.
 */
static append_result serve(const char *crls, const append_upload *up){
  unique_fd fd(open(crls, O_RDWR | O_BINARY));
  if(!fd || !fs_lock(fd.get())) return append_result::io;
  append_state st;
  append_result r = append_load_state(fd.get(), st);
  if(r == append_result::ok) r = append_check(st, up->claim, MAX_BODY);
  if(r == append_result::ok){
    /* Read the body from the client's temporary file, as the server reads a request. */
    uint64_t position = 0;
    r = append_commit(fd.get(), up->claim, [&](std::span<unsigned char> buf){
      bool ok = read_at(up->body_fd(), buf.data(), buf.size(), position);
      position += buf.size();
      return ok;
    });
  }
  return r;
}

/**
 * @brief  Opens the archive, prepares the upload of file and closes the
 *         archive again, as a client would before sending the request.
 * @param  crls     Path of the archive.
 * @param  password Password used to open it.
 * @param  file     Name of the file to upload, inside the test folder.
 * @param  up       Output: the prepared upload.
 * @return True on success, false otherwise.
 */
static bool prepare(const char *crls, const char *password, const char *file, append_upload *up){
  archive a;
  char path[PATH_MAX];
  path_in(path, file);
  bool ok = a.open(crls, password) && up->prepare(a, path);
  a.close();
  return ok;
}

/**
 * @brief  Opens the archive and checks that its entries are exactly names, in
 *         order, and that each one extracts to the original file.
 * @param  crls  Path of the archive.
 * @param  names Expected entry names.
 * @param  count Number of names.
 * @param  out   Name of a new folder for the extracted files.
 */
static void check_contents(const char *crls, const char **names, unsigned int count, const char *out){
  archive a;
  CHECK(a.open(crls, PASSWORD));
  CHECK_EQ(a.entries().size(), count);
  char dir[PATH_MAX];
  path_in(dir, out);
  CHECK_EQ(make_dir(dir), 0);
  for(unsigned int i = 0; i < a.entries().size() && i < count; i++){
    CHECK(a.entries()[i].name == names[i]);
    CHECK(a.extract(i, dir) == extract_result::extracted);
    char original[PATH_MAX], extracted[PATH_MAX];
    path_in(original, names[i]);
    join_path(extracted, sizeof(extracted), dir, names[i]);
    CHECK(same_files(original, extracted));
  }
  a.close();
}

int main(int argc, char **argv){
  if(argc < 2){
    printf("usage: %s <fixtures folder>\n", argv[0]);
    return 2;
  }
  CHECK(archive_crypto_init());
  temp_dir scratch;
  CHECK(static_cast<bool>(scratch));

  make_file("first.bin", 5000, 11);
  make_file("second.bin", 2*CHUNK + 7, 12);
  make_file("third.txt", 300, 13);
  make_file("fourth.bin", 0, 14);

  char crls[PATH_MAX], file[PATH_MAX];
  path_in(crls, "remote.crls");
  archive a;
  CHECK(a.create(crls, PASSWORD, 1, 8u << 20));
  path_in(file, "first.bin");
  CHECK(a.add_file(file));
  a.close();

  /* A valid upload adds the file; the body is only the new entry and the new
     index, never the files that are already there. */
  append_upload up;
  CHECK(prepare(crls, PASSWORD, "second.bin", &up));
  CHECK(up.claim.content_length == up.entry().stored + up.claim.index_length);
  CHECK(serve(crls, &up) == append_result::ok);
  const char *two[] = {"first.bin", "second.bin"};
  check_contents(crls, two, 2, "out1");

  /* A modified signature is rejected. */
  CHECK(prepare(crls, PASSWORD, "third.txt", &up));
  up.claim.signature[5] ^= 1;
  CHECK(serve(crls, &up) == append_result::forbidden);

  /* So is a signature made without the password: a client that opened the
     archive cannot sign once its secret key is replaced. */
  CHECK(a.open(crls, PASSWORD));
  path_in(file, "third.txt");
  randombytes_buf(a.signing_key().data(), SIGN_SK_BYTES);
  CHECK(up.prepare(a, file));
  a.close();
  CHECK(serve(crls, &up) == append_result::forbidden);
  check_contents(crls, two, 2, "out2");

  /* A wrong password cannot even open the archive to prepare an upload. */
  CHECK(!prepare(crls, "wrong-password", "third.txt", &up));

  /* Two clients start from the same state: the second one gets a conflict,
     reloads the archive and succeeds on top of the first. */
  append_upload up_a, up_b;
  CHECK(prepare(crls, PASSWORD, "third.txt", &up_a));
  CHECK(prepare(crls, PASSWORD, "fourth.bin", &up_b));
  CHECK(serve(crls, &up_a) == append_result::ok);
  CHECK(serve(crls, &up_b) == append_result::conflict);
  CHECK(a.open(crls, PASSWORD));
  CHECK(up_b.rebase(a));
  a.close();
  CHECK(serve(crls, &up_b) == append_result::ok);
  const char *four[] = {"first.bin", "second.bin", "third.txt", "fourth.bin"};
  check_contents(crls, four, 4, "out3");

  /* The same name cannot be uploaded twice. */
  CHECK(!prepare(crls, PASSWORD, "third.txt", &up));

  /* A body changed after signing is rejected and leaves the file as it was. */
  make_file("fifth.bin", 4000, 15);
  int fd = open(crls, O_RDONLY | O_BINARY);
  uint64_t size_before = file_size(fd);
  close(fd);
  CHECK(prepare(crls, PASSWORD, "fifth.bin", &up));
  unsigned char byte;
  CHECK(read_at(up.body_fd(), &byte, 1, 10));
  byte ^= 1;
  CHECK(write_at(up.body_fd(), &byte, 1, 10));
  CHECK(serve(crls, &up) == append_result::bad_request);
  fd = open(crls, O_RDONLY | O_BINARY);
  CHECK_EQ(file_size(fd), size_before);
  close(fd);

  /* An index length out of bounds is rejected before the signature. */
  CHECK(prepare(crls, PASSWORD, "fifth.bin", &up));
  up.claim.index_length = 3;
  CHECK(serve(crls, &up) == append_result::bad_request);
  up.claim.index_length = up.claim.content_length + 1;
  CHECK(serve(crls, &up) == append_result::bad_request);

  /* Bytes left after the index by an interrupted upload are removed. */
  fd = open(crls, O_RDWR | O_BINARY);
  CHECK(write_at(fd, "garbage", 7, size_before));
  close(fd);
  CHECK(prepare(crls, PASSWORD, "fifth.bin", &up));
  CHECK(serve(crls, &up) == append_result::ok);
  const char *five[] = {"first.bin", "second.bin", "third.txt", "fourth.bin", "fifth.bin"};
  check_contents(crls, five, 5, "out4");

  /* Version 2 archives have no upload key. */
  char source[PATH_MAX], v2[PATH_MAX];
  join_path(source, sizeof(source), argv[1], "v2.crls");
  path_in(v2, "v2.crls");
  CHECK(copy_file(source, v2));
  fd = open(v2, O_RDWR | O_BINARY);
  append_state st;
  CHECK(append_load_state(fd, st) == append_result::unsupported);
  close(fd);
  CHECK(a.open(v2, "fixture-password"));
  path_in(file, "fifth.bin");
  CHECK(!up.prepare(a, file));
  a.close();

  return test_summary();
}
