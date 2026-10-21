/**
 * @file    test_archive.c
 * @brief   End-to-end tests of core/archive.c, run in a temporary folder.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * No display is required. Runs on Linux and Windows. Run with `make test`.
 *
 * The only argument is the folder holding tests/fixtures/v2.crls, an archive
 * written by the version 2 format code (see tests/fixtures/README.md).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "core/archive.h"
#include "core/status.h"
#include "core/util.h"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#define make_dir(path) _mkdir(path)
#else
#define make_dir(path) mkdir(path, 0700)
#endif

static int failures = 0;

#define CHECK(cond) do{ \
    if(!(cond)){ printf("FAIL %s:%d: %s (status: %s)\n", __FILE__, __LINE__, #cond, status_text); failures++; } \
  }while(0)

static char dir[PATH_MAX];

/**
 * @brief  Creates a new, uniquely named folder for the test files.
 * @return True on success, false otherwise.
 */
static bool make_temp_dir(void){
#ifdef _WIN32
  const char *base = getenv("TEMP");
  snprintf(dir, sizeof(dir), "%s\\file-encryptor-test-XXXXXX", base ? base : ".");
  return _mktemp(dir) != NULL && make_dir(dir) == 0;
#else
  snprintf(dir, sizeof(dir), "/tmp/file-encryptor-test-XXXXXX");
  return mkdtemp(dir) != NULL;
#endif
}

/**
 * @brief Builds the path of name inside the test folder.
 * @param out  Destination buffer of PATH_MAX bytes.
 * @param name File name.
 */
static void path_in(char *out, const char *name){
  join_path(out, PATH_MAX, dir, name);
}

/**
 * @brief Writes size pseudo-random bytes to name inside the test folder.
 *
 * Uses its own xorshift generator instead of rand(), so the contents are the
 * same on every platform; tests/fixtures/v2.crls relies on it.
 *
 * @param name File name.
 * @param size Number of bytes.
 * @param seed Seed of the pseudo-random generator.
 */
static void make_file(const char *name, size_t size, unsigned seed){
  char path[PATH_MAX];
  path_in(path, name);
  FILE *f = fopen(path, "wb");
  uint32_t x = seed * 2654435761u + 1;
  for(size_t i = 0; i < size; i++){
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    fputc((int) (x & 0xff), f);
  }
  fclose(f);
}

/**
 * @brief  Copies a file.
 * @param  from Source path.
 * @param  to   Destination path.
 * @return True on success, false otherwise.
 */
static bool copy_file(const char *from, const char *to){
  FILE *in = fopen(from, "rb"), *out = fopen(to, "wb");
  bool ok = in && out;
  int c;
  while(ok && (c = fgetc(in)) != EOF) ok = fputc(c, out) != EOF;
  if(in) fclose(in);
  if(out && fclose(out) != 0) ok = false;
  return ok;
}

/**
 * @brief  Compares the contents of two files byte by byte.
 * @param  a Path of the first file.
 * @param  b Path of the second file.
 * @return True if both files exist and are identical.
 */
static bool same_files(const char *a, const char *b){
  FILE *x = fopen(a, "rb"), *y = fopen(b, "rb");
  bool same = x && y;
  while(same){
    int c = fgetc(x), d = fgetc(y);
    if(c != d) same = false;
    if(c == EOF || d == EOF) break;
  }
  if(x) fclose(x);
  if(y) fclose(y);
  return same;
}

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
  for(unsigned int i = 0; i < a->entries.n; i++){
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
  CHECK(a.entries.n == 3);
  if(a.entries.n == 3){
    CHECK(strcmp(archive_get_entry(&a, 0)->name, "empty.bin") == 0);
    CHECK(strcmp(archive_get_entry(&a, 1)->name, "small.txt") == 0);
    CHECK(strcmp(archive_get_entry(&a, 2)->name, "chunks.bin") == 0);
  }
  CHECK(archive_wasted_space(&a) > 0);                /* The deleted file and the old indexes. */
  check_extract_all(&a, "out_v2");
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
  CHECK(archive_create(&a, crls, "password123"));
  const char *names[] = {"empty.bin", "small.txt", "exact.bin", "big.bin"};
  for(int i = 0; i < 4; i++){
    path_in(file, names[i]);
    CHECK(archive_add_file(&a, file));
  }
  path_in(file, "small.txt");
  CHECK(!archive_add_file(&a, file));                /* Duplicate name. */
  CHECK(!archive_add_file(&a, crls));                /* Itself. */
  CHECK(a.entries.n == 4);
  CHECK(archive_total_size(&a) == 1000 + CHUNK + 3*CHUNK + 123);
  archive_close(&a);

  /* Reopen. */
  CHECK(!archive_open(&a, crls, "wrong-password"));
  CHECK(archive_open(&a, crls, "password123"));
  CHECK(a.entries.n == 4);
  check_extract_all(&a, "out1");

  /* Delete leaves unused space; compact reclaims it. */
  CHECK(archive_delete_entry(&a, 1));
  CHECK(a.entries.n == 3);
  CHECK(strcmp(archive_get_entry(&a, 1)->name, "exact.bin") == 0);
  CHECK(archive_wasted_space(&a) > 0);
  CHECK(archive_compact(&a));
  CHECK(archive_wasted_space(&a) == 0);
  archive_close(&a);

  CHECK(archive_open(&a, crls, "password123"));
  CHECK(a.entries.n == 3);
  check_extract_all(&a, "out2");
  archive_free(&a);

  char cmd[PATH_MAX + 16];
#ifdef _WIN32
  snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\"", dir);
#else
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
#endif
  if(system(cmd) != 0) printf("could not remove %s\n", dir);

  if(failures) printf("%d check(s) failed\n", failures);
  else printf("All tests passed\n");
  return failures ? 1 : 0;
}
