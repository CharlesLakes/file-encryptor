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

#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include "core/archive.h"
#include "core/fs.h"
#include "core/util.h"
#include "test_util.h"

using namespace crls;

/**
 * @brief  Reads the format version byte of an archive file.
 * @param  crls Path of the archive.
 * @return Version, or -1 if the file cannot be read.
 */
static int header_version(const std::string &crls){
  unique_fd fd(open(crls.c_str(), O_RDONLY | O_BINARY));
  std::array<unsigned char, 1> version;
  if(!fd || !read_at(fd.get(), version, HEADER_VERSION_POS)) return -1;
  return version[0];
}

/**
 * @brief Opens a copy of the version 2 fixture and checks its contents.
 * @param fixtures Folder holding v2.crls.
 */
static void check_v2_fixture(std::string_view fixtures){
  std::string crls = path_in("v2.crls");
  CHECK(copy_file(join_path(fixtures, "v2.crls"), crls));

  /* Same seeds as the generator of the fixture. */
  make_file("empty.bin", 0, 1);
  make_file("small.txt", 1000, 2);
  make_file("chunks.bin", 3*CHUNK + 5, 3);

  archive a;
  CHECK(!a.open(crls, "wrong-password"));
  CHECK(a.open(crls, "fixture-password"));
  CHECK(a.wasted_space() > 0);                /* The deleted file and the old indexes. */
  CHECK_EQ(a.version(), 2);
  constexpr std::array<std::string_view, 3> names = {"empty.bin", "small.txt", "chunks.bin"};
  check_extract(a, "out_v2", names);

  /* A version 2 archive can still be modified, and stays version 2... */
  make_file("added.bin", 70000, 5);
  CHECK(a.add_file(path_in("added.bin")));
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
  check_extract(a, "out_v2_compacted");
  a.close();
}

/**
 * @brief Checks that changing any byte of the authenticated part of a version 3
 *        header makes the archive impossible to open.
 * @param crls     Path of a closed version 3 archive.
 * @param password Its password.
 */
static void check_header_tampering(const std::string &crls, const char *password){
  unique_fd fd(open(crls.c_str(), O_RDWR | O_BINARY));
  CHECK(static_cast<bool>(fd));
  if(!fd) return;
  archive a;
  for(uint64_t i = 0; i < V3_AD_BYTES; i++){
    std::array<unsigned char, 1> byte;
    CHECK(read_at(fd.get(), byte, i));
    byte[0] ^= 0xff;
    CHECK(write_at(fd.get(), byte, i));
    if(a.open(crls, password)){
      fail("archive opened with header byte " + std::to_string(i) + " changed");
      a.close();
    }
    byte[0] ^= 0xff;
    CHECK(write_at(fd.get(), byte, i));
  }
  fd.reset();
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

  std::string crls = path_in("test.crls");
  archive a;

  /* Create and add. */
  /* Low Argon2id limits keep the test fast; archive_create() uses "moderate". */
  CHECK(a.create(crls, "password123", 1, 8u << 20));
  constexpr std::array<std::string_view, 4> names = {"empty.bin", "small.txt", "exact.bin", "big.bin"};
  for(std::string_view name : names) CHECK(a.add_file(path_in(name)));
  CHECK(!a.add_file(path_in("small.txt")));   /* Duplicate name. */
  CHECK(!a.add_file(crls));                   /* Itself. */
  CHECK_EQ(a.entries().size(), 4);
  CHECK(a.total_size() == 1000 + CHUNK + 3*CHUNK + 123);
  CHECK(a.version() == 3 && a.header_bytes() == V3_HEADER_BYTES);
  a.close();
  CHECK_EQ(header_version(crls), 3);
  check_header_tampering(crls, "password123");

  /* Reopen. */
  CHECK(!a.open(crls, "wrong-password"));
  CHECK(a.open(crls, "password123"));
  check_extract(a, "out1", names);

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
  check_extract(a, "out2");
  a.close();

  return test_summary();
}
