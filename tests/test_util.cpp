/**
 * @file    test_util.cpp
 * @brief   Implementation of the test helpers (see test_util.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "test_util.h"

#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "core/util.h"

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#endif

int failures = 0;
char test_dir[PATH_MAX];

int make_dir(const char *path){
#ifdef _WIN32
  return _mkdir(path);
#else
  return mkdir(path, 0700);
#endif
}

bool make_temp_dir(void){
#ifdef _WIN32
  const char *base = getenv("TEMP");
  snprintf(test_dir, sizeof(test_dir), "%s\\file-encryptor-test-XXXXXX", base ? base : ".");
  return _mktemp(test_dir) != NULL && make_dir(test_dir) == 0;
#else
  snprintf(test_dir, sizeof(test_dir), "/tmp/file-encryptor-test-XXXXXX");
  return mkdtemp(test_dir) != NULL;
#endif
}

void remove_temp_dir(void){
  char cmd[PATH_MAX + 16];
#ifdef _WIN32
  snprintf(cmd, sizeof(cmd), "rmdir /s /q \"%s\"", test_dir);
#else
  snprintf(cmd, sizeof(cmd), "rm -rf '%s'", test_dir);
#endif
  if(system(cmd) != 0) printf("could not remove %s\n", test_dir);
}

void path_in(char *out, const char *name){
  join_path(out, PATH_MAX, test_dir, name);
}

void make_file(const char *name, size_t size, unsigned seed){
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

bool copy_file(const char *from, const char *to){
  FILE *in = fopen(from, "rb"), *out = fopen(to, "wb");
  bool ok = in && out;
  int c;
  while(ok && (c = fgetc(in)) != EOF) ok = fputc(c, out) != EOF;
  if(in) fclose(in);
  if(out && fclose(out) != 0) ok = false;
  return ok;
}

bool same_files(const char *a, const char *b){
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

int test_summary(void){
  if(failures) printf("%d check(s) failed\n", failures);
  else printf("All tests passed\n");
  return failures ? 1 : 0;
}
