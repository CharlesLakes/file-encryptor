/**
 * @file    test_archive.cpp
 * @brief   End-to-end tests of core/archive.cpp, run in a temporary folder.
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

using namespace crls;

/**
 * @brief Extracts every entry into out_name and compares each file with its
 *        original.
 * @param a        Open archive.
 * @param out_name Name of the output folder inside the test folder.
 */
static void check_extract_all(archive *a, const char *out_name){
  char out[PATH_MAX];
  path_in(out, out_name);
  CHECK_EQ(make_dir(out), 0);
  for(unsigned int i = 0; i < a->entries().size(); i++){
    const char *name = a->entries()[i].name.c_str();
    CHECK(a->extract(i, out) == extract_result::extracted);
    CHECK(a->extract(i, out) == extract_result::skipped);
    char original[PATH_MAX], extracted[PATH_MAX];
    path_in(original, name);
    snprintf(extracted, sizeof(extracted), "%s", join_path(out, name).c_str());
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
  snprintf(source, sizeof(source), "%s", join_path(fixtures, "v2.crls").c_str());
  path_in(crls, "v2.crls");
  CHECK(copy_file(source, crls));

  /* Same seeds as the generator of the fixture. */
  make_file("empty.bin", 0, 1);
  make_file("small.txt", 1000, 2);
  make_file("chunks.bin", 3*CHUNK + 5, 3);

  archive a;
  CHECK(!a.open(crls, "wrong-password"));
  CHECK(a.open(crls, "fixture-password"));
  CHECK_EQ(a.entries().size(), 3);
  if(a.entries().size() == 3){
    CHECK_EQ(a.entries()[0].name, "empty.bin");
    CHECK_EQ(a.entries()[1].name, "small.txt");
    CHECK_EQ(a.entries()[2].name, "chunks.bin");
  }
  CHECK(a.wasted_space() > 0);                /* The deleted file and the old indexes. */
  CHECK_EQ(a.version(), 2);
  check_extract_all(&a, "out_v2");

  /* A version 2 archive can still be modified, and stays version 2... */
  char file[PATH_MAX];
  make_file("added.bin", 70000, 5);
  path_in(file, "added.bin");
  CHECK(a.add_file(file));
  CHECK_EQ(a.version(), 2);

  /* ...until it is compacted, which converts it to version 3. */
  CHECK(a.compact());
  CHECK_EQ(a.version(), 3);
  CHECK_EQ(a.wasted_space(), 0);
  a.close();

  CHECK_EQ(header_version(crls), 3);
  CHECK(!a.open(crls, "wrong-password"));
  CHECK(a.open(crls, "fixture-password"));
  CHECK(a.version() == 3 && a.entries().size() == 4);
  check_extract_all(&a, "out_v2_compacted");
  a.close();
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
  for(long i = 0; i < static_cast<long>(V3_AD_BYTES); i++){
    fseek(f, i, SEEK_SET);
    int c = fgetc(f);
    fseek(f, i, SEEK_SET);
    fputc(c ^ 0xff, f);
    fflush(f);
    if(a.open(crls, password)){
      printf("FAIL: archive opened with header byte %ld changed\n", i);
      failures++;
      a.close();
    }
    fseek(f, i, SEEK_SET);
    fputc(c, f);
    fflush(f);
  }
  fclose(f);
  CHECK(a.open(crls, password));
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
  check_v2_fixture(argv[1]);

  make_file("empty.bin", 0, 1);
  make_file("small.txt", 1000, 2);
  make_file("exact.bin", CHUNK, 3);
  make_file("big.bin", 3*CHUNK + 123, 4);

  char crls[PATH_MAX], file[PATH_MAX];
  path_in(crls, "test.crls");

  archive a;

  /* Create and add. */
  /* Low Argon2id limits keep the test fast; archive_create() uses "moderate". */
  CHECK(a.create(crls, "password123", 1, 8u << 20));
  const char *names[] = {"empty.bin", "small.txt", "exact.bin", "big.bin"};
  for(int i = 0; i < 4; i++){
    path_in(file, names[i]);
    CHECK(a.add_file(file));
  }
  path_in(file, "small.txt");
  CHECK(!a.add_file(file));                /* Duplicate name. */
  CHECK(!a.add_file(crls));                /* Itself. */
  CHECK_EQ(a.entries().size(), 4);
  CHECK(a.total_size() == 1000 + CHUNK + 3*CHUNK + 123);
  CHECK(a.version() == 3 && a.header_bytes() == V3_HEADER_BYTES);
  a.close();
  CHECK_EQ(header_version(crls), 3);
  check_header_tampering(crls, "password123");

  /* Reopen. */
  CHECK(!a.open(crls, "wrong-password"));
  CHECK(a.open(crls, "password123"));
  CHECK_EQ(a.entries().size(), 4);
  check_extract_all(&a, "out1");

  /* Delete leaves unused space; compact reclaims it. */
  CHECK(a.remove(1));
  CHECK_EQ(a.entries().size(), 3);
  CHECK_EQ(a.entries()[1].name, "exact.bin");
  CHECK(a.wasted_space() > 0);
  CHECK(a.compact());
  CHECK_EQ(a.wasted_space(), 0);
  a.close();

  CHECK(a.open(crls, "password123"));
  CHECK_EQ(a.entries().size(), 3);
  check_extract_all(&a, "out2");
  a.close();

  return test_summary();
}
