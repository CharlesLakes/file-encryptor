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

#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include "core/append.h"
#include "core/archive.h"
#include "core/fs.h"
#include "core/util.h"
#include "deps/sodium.h"
#include "test_util.h"

using namespace crls;

inline constexpr char PASSWORD[] = "upload-password";
inline constexpr uint64_t MAX_BODY = 64ULL << 20;

/**
 * @brief  Runs the server side of an append on the archive at crls.
 * @param  crls Path of the archive (must not be open elsewhere).
 * @param  up   Upload prepared by a client.
 * @return Result of the first step that did not succeed, or append_result::ok.
 */
static append_result serve(const std::string &crls, const append_upload &up){
  unique_fd fd(open(crls.c_str(), O_RDWR | O_BINARY));
  if(!fd || !fs_lock(fd.get())) return append_result::io;
  append_state st;
  append_result r = append_load_state(fd.get(), st);
  if(r == append_result::ok) r = append_check(st, up.claim, MAX_BODY);
  if(r == append_result::ok){
    /* Read the body from the client's temporary file, as the server reads a request. */
    uint64_t position = 0;
    r = append_commit(fd.get(), up.claim, [&](std::span<unsigned char> buf){
      bool ok = read_at(up.body_fd(), buf, position);
      position += buf.size();
      return ok;
    });
  }
  return r;
}

/**
 * @brief Opens the archive and checks that its entries are exactly names, in
 *        order, and that each one extracts to the original file.
 * @param crls  Path of the archive.
 * @param names Expected entry names.
 * @param out   Name of a new folder for the extracted files.
 */
static void check_contents(const std::string &crls, std::span<const std::string_view> names, std::string_view out){
  archive a;
  CHECK(a.open(crls, PASSWORD));
  check_extract(a, out, names);
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

  std::string crls = path_in("remote.crls");
  archive a;
  CHECK(a.create(crls, PASSWORD, 1, 8u << 20));
  CHECK(a.add_file(path_in("first.bin")));
  a.close();

  /* A valid upload adds the file; the body is only the new entry and the new
     index, never the files that are already there. */
  append_upload up;
  CHECK(prepare_upload(crls, PASSWORD, "second.bin", up));
  CHECK(up.claim.content_length == up.entry().stored + up.claim.index_length);
  CHECK(serve(crls, up) == append_result::ok);
  constexpr std::array<std::string_view, 2> two = {"first.bin", "second.bin"};
  check_contents(crls, two, "out1");

  /* A modified signature is rejected. */
  CHECK(prepare_upload(crls, PASSWORD, "third.txt", up));
  up.claim.signature[5] ^= 1;
  CHECK(serve(crls, up) == append_result::forbidden);

  /* So is a signature made without the password: a client that opened the
     archive cannot sign once its secret key is replaced. */
  CHECK(a.open(crls, PASSWORD));
  std::span<unsigned char> key = a.signing_key();
  randombytes_buf(key.data(), key.size());
  CHECK(up.prepare(a, path_in("third.txt")));
  a.close();
  CHECK(serve(crls, up) == append_result::forbidden);
  check_contents(crls, two, "out2");

  /* A wrong password cannot even open the archive to prepare an upload. */
  CHECK(!prepare_upload(crls, "wrong-password", "third.txt", up));

  /* Two clients start from the same state: the second one gets a conflict,
     reloads the archive and succeeds on top of the first. */
  append_upload up_a, up_b;
  CHECK(prepare_upload(crls, PASSWORD, "third.txt", up_a));
  CHECK(prepare_upload(crls, PASSWORD, "fourth.bin", up_b));
  CHECK(serve(crls, up_a) == append_result::ok);
  CHECK(serve(crls, up_b) == append_result::conflict);
  CHECK(a.open(crls, PASSWORD));
  CHECK(up_b.rebase(a));
  a.close();
  CHECK(serve(crls, up_b) == append_result::ok);
  constexpr std::array<std::string_view, 4> four = {"first.bin", "second.bin", "third.txt", "fourth.bin"};
  check_contents(crls, four, "out3");

  /* The same name cannot be uploaded twice. */
  CHECK(!prepare_upload(crls, PASSWORD, "third.txt", up));

  /* A body changed after signing is rejected and leaves the file as it was. */
  make_file("fifth.bin", 4000, 15);
  uint64_t size_before = size_of(crls);
  CHECK(prepare_upload(crls, PASSWORD, "fifth.bin", up));
  std::array<unsigned char, 1> byte;
  CHECK(read_at(up.body_fd(), byte, 10));
  byte[0] ^= 1;
  CHECK(write_at(up.body_fd(), byte, 10));
  CHECK(serve(crls, up) == append_result::bad_request);
  CHECK_EQ(size_of(crls), size_before);

  /* An index length out of bounds is rejected before the signature. */
  CHECK(prepare_upload(crls, PASSWORD, "fifth.bin", up));
  up.claim.index_length = 3;
  CHECK(serve(crls, up) == append_result::bad_request);
  up.claim.index_length = up.claim.content_length + 1;
  CHECK(serve(crls, up) == append_result::bad_request);

  /* Bytes left after the index by an interrupted upload are removed. */
  {
    unique_fd fd(open(crls.c_str(), O_RDWR | O_BINARY));
    constexpr std::array<unsigned char, 7> garbage = {'g', 'a', 'r', 'b', 'a', 'g', 'e'};
    CHECK(fd && write_at(fd.get(), garbage, size_before));
  }
  CHECK(prepare_upload(crls, PASSWORD, "fifth.bin", up));
  CHECK(serve(crls, up) == append_result::ok);
  constexpr std::array<std::string_view, 5> five = {"first.bin", "second.bin", "third.txt", "fourth.bin", "fifth.bin"};
  check_contents(crls, five, "out4");

  /* Version 2 archives have no upload key. */
  std::string v2 = path_in("v2.crls");
  CHECK(copy_file(join_path(argv[1], "v2.crls"), v2));
  {
    unique_fd fd(open(v2.c_str(), O_RDWR | O_BINARY));
    append_state st;
    CHECK(fd && append_load_state(fd.get(), st) == append_result::unsupported);
  }
  CHECK(a.open(v2, "fixture-password"));
  CHECK(!up.prepare(a, path_in("fifth.bin")));
  a.close();

  return test_summary();
}
