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

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "core/archive.h"
#include "core/fs.h"
#include "core/remote.h"
#include "core/util.h"
#include "server_fixture.h"
#include "test_util.h"

using namespace crls;

#define PASSWORD "remote-password"
inline constexpr int SKIP = 77;   /**< Exit code that CTest reports as skipped. */

/**
 * @brief  Returns the size of a local file.
 * @param  path Path of the file.
 * @return Size in bytes, or 0 if it cannot be opened.
 */
static uint64_t size_of(const char *path){
  int fd = open(path, O_RDONLY | O_BINARY);
  uint64_t size = fd >= 0 ? file_size(fd) : 0;
  if(fd >= 0) close(fd);
  return size;
}

/**
 * @brief Extracts every entry of an open archive into a new folder and
 *        compares each file with the original of the same name.
 * @param a   Open archive.
 * @param out Name of the new folder inside the test folder.
 */
static void check_extract(archive *a, const char *out){
  char dir[PATH_MAX];
  path_in(dir, out);
  CHECK_EQ(make_dir(dir), 0);
  for(unsigned int i = 0; i < a->entries().size(); i++){
    const char *name = a->entries()[i].name.c_str();
    char original[PATH_MAX], extracted[PATH_MAX];
    CHECK(a->extract(i, dir) == extract_result::extracted);
    path_in(original, name);
    snprintf(extracted, sizeof(extracted), "%s", join_path(dir, name).c_str());
    CHECK(same_files(original, extracted));
  }
}

int main(){
  CHECK(archive_crypto_init());
  temp_dir scratch;
  CHECK(static_cast<bool>(scratch));

  char served[PATH_MAX], crls[PATH_MAX], file[PATH_MAX], url[256];
  path_in(served, "served");
  CHECK_EQ(make_dir(served), 0);
  snprintf(crls, sizeof(crls), "%s", join_path(served, "shared.crls").c_str());
  /* Larger than the 1 MiB read block, to go through several Range requests. */
  make_file("big.bin", 3 * REMOTE_BLOCK + 12345, 31);
  make_file("small.txt", 777, 32);
  make_file("upload-a.bin", 100000, 33);
  make_file("upload-b.bin", 54321, 34);
  make_file("upload-c.bin", 10, 35);

  archive a, b;
  CHECK(a.create(crls, PASSWORD, 1, 8u << 20));
  path_in(file, "big.bin");
  CHECK(a.add_file(file));
  path_in(file, "small.txt");
  CHECK(a.add_file(file));
  a.close();

  test_server server(served);
  int port = server.port();
  CHECK(port > 0);
  snprintf(url, sizeof(url), "http://127.0.0.1:%d/shared.crls", port);

  /* Without the password nothing is visible. */
  CHECK(!remote_open(a, url, "wrong-password"));
  CHECK(!a.is_open() && a.entries().size() == 0);
  if(strstr(status_message(), "libcurl") != NULL){
    printf("SKIP: %s\n", status_message());
    return SKIP;
  }

  /* With it, the files are listed and extracted byte for byte. */
  CHECK(remote_open(a, url, PASSWORD));
  CHECK(a.is_remote() && a.entries().size() == 2);
  check_extract(&a, "out1");

  /* Uploading sends only the new file and the new index: the archive on the
     server grows by exactly that. */
  uint64_t before = size_of(crls);
  path_in(file, "upload-a.bin");
  CHECK(remote_add_file(a, file));
  CHECK_EQ(a.entries().size(), 3);
  const archive_entry &added = a.entries()[2];
  CHECK(size_of(crls) == before + added.stored + a.index_length());

  /* Two clients: b opened before a's next upload, so b's first attempt
     conflicts and is rebuilt on top of it. */
  CHECK(remote_open(b, url, PASSWORD));
  path_in(file, "upload-b.bin");
  CHECK(remote_add_file(a, file));
  path_in(file, "upload-c.bin");
  CHECK(remote_add_file(b, file));
  CHECK_EQ(b.entries().size(), 5);
  b.close();

  /* A client that does not know the password cannot upload. */
  CHECK(remote_open(b, url, PASSWORD));
  memset(b.signing_key().data(), 9, SIGN_SK_BYTES);
  path_in(file, "small.txt");
  CHECK(!remote_add_file(b, file));                 /* Duplicate name, rejected locally. */
  make_file("upload-d.bin", 5, 36);
  path_in(file, "upload-d.bin");
  CHECK(!remote_add_file(b, file));
  CHECK(strstr(status_message(), "password") != NULL);
  b.close();

  /* Everything is there when the archive is opened again, remotely or locally. */
  a.close();
  CHECK(remote_open(a, url, PASSWORD));
  CHECK_EQ(a.entries().size(), 5);
  check_extract(&a, "out2");

  /* Compacting the archive on the server replaces the file: the open remote
     archive notices and asks to be opened again. */
  archive local;
  CHECK(local.open(crls, PASSWORD));
  CHECK_EQ(local.entries().size(), 5);
  CHECK(local.remove(4));
  CHECK(local.compact());
  local.close();
  char dir[PATH_MAX];
  path_in(dir, "out3");
  CHECK_EQ(make_dir(dir), 0);
  CHECK(a.extract(0, dir) == extract_result::failed);
  CHECK(strstr(status_message(), "Open it again") != NULL);
  a.close();
  b.close();

  return test_summary();
}
