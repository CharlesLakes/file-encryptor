/**
 * @file    test_cli.cpp
 * @brief   Tests of `--upload` (cli/cli.c) against the built-in server, with
 *          the password given on standard input as a script would.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * Needs libcurl on Linux; when it is not installed the test is skipped
 * (exit code 77).
 */

#include <stdio.h>
#include <string.h>
#include "cli/cli.h"
#include "core/archive.h"
#include "core/remote.h"
#include "core/util.h"
#include "server_fixture.h"
#include "test_util.h"

using namespace crls;

#define PASSWORD "cli-password"
#define SKIP 77

/**
 * @brief  Runs cli_upload() with the given password on standard input.
 * @param  password Line given on standard input.
 * @param  argc     Number of arguments, starting at "--upload".
 * @param  argv     Arguments, starting at "--upload".
 * @return Exit code of cli_upload().
 */
static int run_upload(const char *password, int argc, char **argv){
  char input[PATH_MAX];
  path_in(input, "stdin.txt");
  FILE *f = fopen(input, "w");
  fprintf(f, "%s\n", password);
  fclose(f);
  if(!freopen(input, "r", stdin)) return -1;
  return cli_upload(argc, argv);
}

int main(void){
  CHECK(archive_crypto_init());
  CHECK(make_temp_dir());

  char served[PATH_MAX], crls[PATH_MAX], url[256], one[PATH_MAX], two[PATH_MAX];
  path_in(served, "served");
  CHECK(make_dir(served) == 0);
  join_path(crls, sizeof(crls), served, "cli.crls");
  make_file("one.bin", 2000, 41);
  make_file("two.bin", 3000, 42);
  path_in(one, "one.bin");
  path_in(two, "two.bin");

  archive a;
  archive_init(&a);
  CHECK(archive_create_params(&a, crls, PASSWORD, 1, 8u << 20));
  archive_close(&a);

  int port = start_test_server(served);
  CHECK(port > 0);
  snprintf(url, sizeof(url), "http://127.0.0.1:%d/cli.crls", port);

  /* argv is writable in C and C++, string literals are not. */
  char upload[] = "--upload";
  char *usage[] = {upload, url};
  CHECK(cli_upload(2, usage) == 2);

  /* The wrong password opens nothing and uploads nothing. */
  char *args[] = {upload, url, one, two};
  int code = run_upload("not-the-password", 4, args);
  if(strstr(status_text, "libcurl") != NULL){
    printf("SKIP: %s\n", status_text);
    stop_test_server();
    remove_temp_dir();
    return SKIP;
  }
  CHECK(code == 1);

  CHECK(run_upload(PASSWORD, 4, args) == 0);
  /* Uploading the same names again fails for each file. */
  CHECK(run_upload(PASSWORD, 4, args) == 1);
  stop_test_server();

  CHECK(archive_open(&a, crls, PASSWORD));
  CHECK(a.entries.size() == 2);
  if(a.entries.size() == 2){
    CHECK(strcmp(archive_get_entry(&a, 0)->name, "one.bin") == 0);
    CHECK(strcmp(archive_get_entry(&a, 1)->name, "two.bin") == 0);
  }
  archive_free(&a);

  remove_temp_dir();
  return test_summary();
}
