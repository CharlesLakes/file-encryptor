/**
 * @file    test_remote.cpp
 * @brief   End-to-end tests of archives at URLs (core/remote.cpp) against the
 *          built-in server on http://127.0.0.1.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * Needs libcurl on Linux; when it is not installed the test is skipped
 * (exit code 77).
 */

#include <algorithm>
#include <cstdio>
#include <string>
#include "core/archive.hpp"
#include "core/remote.hpp"
#include "core/status.hpp"
#include "core/util.hpp"
#include "server_fixture.hpp"
#include "test_util.hpp"

using namespace crls;

inline constexpr char PASSWORD[] = "remote-password";
inline constexpr int SKIP = 77;   /**< Exit code that CTest reports as skipped. */

int main(){
  CHECK(archive_crypto_init());
  temp_dir scratch;
  CHECK(static_cast<bool>(scratch));

  std::string served = path_in("served");
  CHECK(make_dir(served));
  std::string crls = join_path(served, "shared.crls");
  /* Larger than the 1 MiB read block, to go through several Range requests. */
  make_file("big.bin", 3 * REMOTE_BLOCK + 12345, 31);
  make_file("small.txt", 777, 32);
  make_file("upload-a.bin", 100000, 33);
  make_file("upload-b.bin", 54321, 34);
  make_file("upload-c.bin", 10, 35);

  archive a, b;
  CHECK(a.create(crls, PASSWORD, 1, 8u << 20));
  CHECK(a.add_file(path_in("big.bin")));
  CHECK(a.add_file(path_in("small.txt")));
  a.close();

  test_server server(served);
  CHECK(server.port() > 0);
  std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/shared.crls";

  /* Without the password nothing is visible. */
  CHECK(!remote_open(a, url, "wrong-password"));
  CHECK(!a.is_open() && a.entries().empty());
  if(status_mentions("libcurl")){
    printf("SKIP: %s\n", status_message());
    return SKIP;
  }

  /* With it, the files are listed and extracted byte for byte. */
  CHECK(remote_open(a, url, PASSWORD));
  CHECK(a.is_remote() && a.entries().size() == 2);
  check_extract(a, "out1");

  /* Uploading sends only the new file and the new index: the archive on the
     server grows by exactly that. */
  uint64_t before = size_of(crls);
  CHECK(remote_add_file(a, path_in("upload-a.bin")));
  CHECK_EQ(a.entries().size(), 3);
  const archive_entry &added = a.entries()[2];
  CHECK(size_of(crls) == before + added.stored + a.index_length());

  /* Two clients: b opened before a's next upload, so b's first attempt
     conflicts and is rebuilt on top of it. */
  CHECK(remote_open(b, url, PASSWORD));
  CHECK(remote_add_file(a, path_in("upload-b.bin")));
  CHECK(remote_add_file(b, path_in("upload-c.bin")));
  CHECK_EQ(b.entries().size(), 5);
  b.close();

  /* A client that does not know the password cannot upload. */
  CHECK(remote_open(b, url, PASSWORD));
  std::ranges::fill(b.signing_key(), 9);
  CHECK(!remote_add_file(b, path_in("small.txt")));       /* Duplicate name, rejected locally. */
  make_file("upload-d.bin", 5, 36);
  CHECK(!remote_add_file(b, path_in("upload-d.bin")));
  CHECK(status_mentions("password"));
  b.close();

  /* Everything is there when the archive is opened again, remotely or locally. */
  a.close();
  CHECK(remote_open(a, url, PASSWORD));
  CHECK_EQ(a.entries().size(), 5);
  check_extract(a, "out2");

  /* Compacting the archive on the server replaces the file: the open remote
     archive notices and asks to be opened again. */
  archive local;
  CHECK(local.open(crls, PASSWORD));
  CHECK_EQ(local.entries().size(), 5);
  CHECK(local.remove(4));
  CHECK(local.compact());
  local.close();
  std::string out = path_in("out3");
  CHECK(make_dir(out));
  CHECK(a.extract(0, out) == extract_result::failed);
  CHECK(status_mentions("Open it again"));
  a.close();
  b.close();

  return test_summary();
}
