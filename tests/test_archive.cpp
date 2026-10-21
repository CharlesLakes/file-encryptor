/**
 * @file    test_archive.cpp
 * @brief   End-to-end tests of core/archive.c, run in a temporary folder.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * No display is required. Runs on Linux and Windows. Run with `ctest`.
 *
 * The only argument is the folder holding tests/fixtures/v2.crls, an archive
 * written by the version 2 format code (see tests/fixtures/README.md).
 */

#include <stdio.h>
#include <string.h>
#include "core/archive.h"
#include "core/util.h"
#include "test_util.h"

/**
 * @brief Extracts every entry into out_name and compares each file with its
 *        original.
 * @param a        Open archive.
 * @param out_name Name of the output folder inside the test folder.
 */
static void check_extract_all(archive *a, const char *out_name){
  char out[PATH_MAX];
  path_in(out, out_name);
  CHECK(make_dir(out) == 0);
  for(unsigned int i = 0; i < a->entries.size(); i++){
    const char *name = archive_get_entry(a, i)->name;
    CHECK(archive_extract_entry(a, i, out) == 1);
    CHECK(archive_extract_entry(a, i, out) == 0);   /* Already exists: skipped. */
    char original[PATH_MAX], extracted[PATH_MAX];
    path_in(original, name);
    join_path(extracted, sizeof(extracted), out, name);
    CHECK(same_files(original, extracted));
  }
}

/**
 * @brief  Reads the format version byte of an archive file.
 * @param  crls Path of the archive.
 * @return Version, or -1 if the file cannot be read.
 */
static int header_version(const char *crls){
  FILE *f = fopen(crls, "rb");
  if(!f) return -1;
  fseek(f, 6, SEEK_SET);
  int version = fgetc(f);
  fclose(f);
  return version;
}

/**
 * @brief Opens a copy of the version 2 fixture and checks its contents.
 * @param fixtures Folder holding v2.crls.
 */
static void check_v2_fixture(const char *fixtures){
  char source[PATH_MAX], crls[PATH_MAX];
  join_path(source, sizeof(source), fixtures, "v2.crls");
  path_in(crls, "v2.crls");
  CHECK(copy_file(source, crls));

  /* Same seeds as the generator of the fixture. */
  make_file("empty.bin", 0, 1);
  make_file("small.txt", 1000, 2);
  make_file("chunks.bin", 3*CHUNK + 5, 3);

  archive a;
  archive_init(&a);
  CHECK(!archive_open(&a, crls, "wrong-password"));
  CHECK(archive_open(&a, crls, "fixture-password"));
  CHECK(a.entries.size() == 3);
  if(a.entries.size() == 3){
    CHECK(strcmp(archive_get_entry(&a, 0)->name, "empty.bin") == 0);
    CHECK(strcmp(archive_get_entry(&a, 1)->name, "small.txt") == 0);
    CHECK(strcmp(archive_get_entry(&a, 2)->name, "chunks.bin") == 0);
  }
  CHECK(archive_wasted_space(&a) > 0);                /* The deleted file and the old indexes. */
  CHECK(a.version == 2);
  check_extract_all(&a, "out_v2");

  /* A version 2 archive can still be modified, and stays version 2... */
  char file[PATH_MAX];
  make_file("added.bin", 70000, 5);
  path_in(file, "added.bin");
  CHECK(archive_add_file(&a, file));
  CHECK(a.version == 2);

  /* ...until it is compacted, which converts it to version 3. */
  CHECK(archive_compact(&a));
  CHECK(a.version == 3);
  CHECK(archive_wasted_space(&a) == 0);
  archive_close(&a);

  CHECK(header_version(crls) == 3);
  CHECK(!archive_open(&a, crls, "wrong-password"));
  CHECK(archive_open(&a, crls, "fixture-password"));
  CHECK(a.version == 3 && a.entries.size() == 4);
  check_extract_all(&a, "out_v2_compacted");
  archive_free(&a);
}

/**
 * @brief Checks that changing any byte of the authenticated part of a version 3
 *        header makes the archive impossible to open.
 * @param crls     Path of a closed version 3 archive.
 * @param password Its password.
 */
static void check_header_tampering(const char *crls, const char *password){
  FILE *f = fopen(crls, "r+b");
  CHECK(f != NULL);
  if(!f) return;
  archive a;
  archive_init(&a);
  for(long i = 0; i < V3_AD_BYTES; i++){
    fseek(f, i, SEEK_SET);
    int c = fgetc(f);
    fseek(f, i, SEEK_SET);
    fputc(c ^ 0xff, f);
    fflush(f);
    if(archive_open(&a, crls, password)){
      printf("FAIL: archive opened with header byte %ld changed\n", i);
      failures++;
      archive_close(&a);
    }
    fseek(f, i, SEEK_SET);
    fputc(c, f);
    fflush(f);
  }
  fclose(f);
  CHECK(archive_open(&a, crls, password));
  archive_free(&a);
}

int main(int argc, char **argv){
  if(argc < 2){
    printf("usage: %s <fixtures folder>\n", argv[0]);
    return 2;
  }
  CHECK(archive_crypto_init());
  CHECK(make_temp_dir());
  check_v2_fixture(argv[1]);

  make_file("empty.bin", 0, 1);
  make_file("small.txt", 1000, 2);
  make_file("exact.bin", CHUNK, 3);
  make_file("big.bin", 3*CHUNK + 123, 4);

  char crls[PATH_MAX], file[PATH_MAX];
  path_in(crls, "test.crls");

  archive a;
  archive_init(&a);

  /* Create and add. */
  /* Low Argon2id limits keep the test fast; archive_create() uses "moderate". */
  CHECK(archive_create_params(&a, crls, "password123", 1, 8u << 20));
  const char *names[] = {"empty.bin", "small.txt", "exact.bin", "big.bin"};
  for(int i = 0; i < 4; i++){
    path_in(file, names[i]);
    CHECK(archive_add_file(&a, file));
  }
  path_in(file, "small.txt");
  CHECK(!archive_add_file(&a, file));                /* Duplicate name. */
  CHECK(!archive_add_file(&a, crls));                /* Itself. */
  CHECK(a.entries.size() == 4);
  CHECK(archive_total_size(&a) == 1000 + CHUNK + 3*CHUNK + 123);
  CHECK(a.version == 3 && a.header_bytes == V3_HEADER_BYTES);
  archive_close(&a);
  CHECK(header_version(crls) == 3);
  check_header_tampering(crls, "password123");

  /* Reopen. */
  CHECK(!archive_open(&a, crls, "wrong-password"));
  CHECK(archive_open(&a, crls, "password123"));
  CHECK(a.entries.size() == 4);
  check_extract_all(&a, "out1");

  /* Delete leaves unused space; compact reclaims it. */
  CHECK(archive_delete_entry(&a, 1));
  CHECK(a.entries.size() == 3);
  CHECK(strcmp(archive_get_entry(&a, 1)->name, "exact.bin") == 0);
  CHECK(archive_wasted_space(&a) > 0);
  CHECK(archive_compact(&a));
  CHECK(archive_wasted_space(&a) == 0);
  archive_close(&a);

  CHECK(archive_open(&a, crls, "password123"));
  CHECK(a.entries.size() == 3);
  check_extract_all(&a, "out2");
  archive_free(&a);

  remove_temp_dir();
  return test_summary();
}
