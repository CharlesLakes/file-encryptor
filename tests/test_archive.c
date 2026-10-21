/**
 * @file    test_archive.c
 * @brief   End-to-end tests of core/archive.c, run in a temporary folder.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * No X11 display is required. Run with `make test`.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "core/archive.h"
#include "core/status.h"
#include "core/util.h"

static int failures = 0;

#define CHECK(cond) do{ \
    if(!(cond)){ printf("FAIL %s:%d: %s (status: %s)\n", __FILE__, __LINE__, #cond, status_text); failures++; } \
  }while(0)

static char dir[] = "/tmp/file-encrypter-test-XXXXXX";

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
 * @param name File name.
 * @param size Number of bytes.
 * @param seed Seed of the pseudo-random generator.
 */
static void make_file(const char *name, size_t size, unsigned seed){
  char path[PATH_MAX];
  path_in(path, name);
  FILE *f = fopen(path, "wb");
  srand(seed);
  for(size_t i = 0; i < size; i++) fputc(rand() & 0xff, f);
  fclose(f);
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
  CHECK(mkdir(out, 0700) == 0);
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

int main(void){
  CHECK(archive_crypto_init());
  CHECK(mkdtemp(dir) != NULL);

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
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", dir);
  if(system(cmd) != 0) printf("could not remove %s\n", dir);

  if(failures) printf("%d check(s) failed\n", failures);
  else printf("All tests passed\n");
  return failures ? 1 : 0;
}
