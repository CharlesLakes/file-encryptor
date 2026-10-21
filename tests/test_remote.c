/**
 * @file    test_remote.c
 * @brief   End-to-end tests of archives at URLs (core/remote.c) against the
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

#define PASSWORD "remote-password"
#define SKIP 77

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
  CHECK(make_dir(dir) == 0);
  for(unsigned int i = 0; i < a->entries.n; i++){
    const char *name = archive_get_entry(a, i)->name;
    char original[PATH_MAX], extracted[PATH_MAX];
    CHECK(archive_extract_entry(a, i, dir) == 1);
    path_in(original, name);
    join_path(extracted, sizeof(extracted), dir, name);
    CHECK(same_files(original, extracted));
  }
}

int main(void){
  CHECK(archive_crypto_init());
  CHECK(make_temp_dir());

  char served[PATH_MAX], crls[PATH_MAX], file[PATH_MAX], url[256];
  path_in(served, "served");
  CHECK(make_dir(served) == 0);
  join_path(crls, sizeof(crls), served, "shared.crls");
  /* Larger than the 1 MiB read block, to go through several Range requests. */
  make_file("big.bin", 3 * REMOTE_BLOCK + 12345, 31);
  make_file("small.txt", 777, 32);
  make_file("upload-a.bin", 100000, 33);
  make_file("upload-b.bin", 54321, 34);
  make_file("upload-c.bin", 10, 35);

  archive a, b;
  archive_init(&a);
  archive_init(&b);
  CHECK(archive_create_params(&a, crls, PASSWORD, 1, 8u << 20));
  path_in(file, "big.bin");
  CHECK(archive_add_file(&a, file));
  path_in(file, "small.txt");
  CHECK(archive_add_file(&a, file));
  archive_close(&a);

  int port = start_test_server(served);
  CHECK(port > 0);
  snprintf(url, sizeof(url), "http://127.0.0.1:%d/shared.crls", port);

  /* Without the password nothing is visible. */
  CHECK(!remote_open(&a, url, "wrong-password"));
  CHECK(!a.is_open && a.entries.n == 0);
  if(strstr(status_text, "libcurl") != NULL){
    printf("SKIP: %s\n", status_text);
    stop_test_server();
    remove_temp_dir();
    return SKIP;
  }

  /* With it, the files are listed and extracted byte for byte. */
  CHECK(remote_open(&a, url, PASSWORD));
  CHECK(a.is_remote && a.entries.n == 2);
  check_extract(&a, "out1");

  /* Uploading sends only the new file and the new index: the archive on the
     server grows by exactly that. */
  uint64_t before = size_of(crls);
  path_in(file, "upload-a.bin");
  CHECK(remote_add_file(&a, file));
  CHECK(a.entries.n == 3);
  archive_entry *added = archive_get_entry(&a, 2);
  CHECK(size_of(crls) == before + added->stored + a.index_length);

  /* Two clients: b opened before a's next upload, so b's first attempt
     conflicts and is rebuilt on top of it. */
  CHECK(remote_open(&b, url, PASSWORD));
  path_in(file, "upload-b.bin");
  CHECK(remote_add_file(&a, file));
  path_in(file, "upload-c.bin");
  CHECK(remote_add_file(&b, file));
  CHECK(b.entries.n == 5);
  archive_close(&b);

  /* A client that does not know the password cannot upload. */
  CHECK(remote_open(&b, url, PASSWORD));
  memset(b.sign_sk, 9, sizeof(b.sign_sk));
  path_in(file, "small.txt");
  CHECK(!remote_add_file(&b, file));                 /* Duplicate name, rejected locally. */
  make_file("upload-d.bin", 5, 36);
  path_in(file, "upload-d.bin");
  CHECK(!remote_add_file(&b, file));
  CHECK(strstr(status_text, "password") != NULL);
  archive_close(&b);

  /* Everything is there when the archive is opened again, remotely or locally. */
  archive_close(&a);
  CHECK(remote_open(&a, url, PASSWORD));
  CHECK(a.entries.n == 5);
  check_extract(&a, "out2");

  /* Compacting the archive on the server replaces the file: the open remote
     archive notices and asks to be opened again. */
  archive local;
  archive_init(&local);
  CHECK(archive_open(&local, crls, PASSWORD));
  CHECK(local.entries.n == 5);
  CHECK(archive_delete_entry(&local, 4));
  CHECK(archive_compact(&local));
  archive_free(&local);
  char dir[PATH_MAX];
  path_in(dir, "out3");
  CHECK(make_dir(dir) == 0);
  CHECK(archive_extract_entry(&a, 0, dir) == -1);
  CHECK(strstr(status_text, "Open it again") != NULL);
  archive_free(&a);
  archive_free(&b);

  stop_test_server();
  remove_temp_dir();
  return test_summary();
}
