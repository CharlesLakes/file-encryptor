/**
 * @file    test_cli.cpp
 * @brief   Tests of `--upload` (cli/cli.cpp) against the built-in server, with
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
inline constexpr int SKIP = 77;   /**< Exit code that CTest reports as skipped. */

/**
 * @brief  Runs cli_upload() with the given password on standard input.
 * @param  password Line given on standard input.
 * @param  args     Arguments, starting at "--upload".
 * @return Exit code of cli_upload().
 */
static int run_upload(const char *password, std::span<char *const> args){
  char input[PATH_MAX];
  path_in(input, "stdin.txt");
  FILE *f = fopen(input, "w");
  fprintf(f, "%s\n", password);
  fclose(f);
  if(!freopen(input, "r", stdin)) return -1;
  return cli_upload(args);
}

int main(){
  CHECK(archive_crypto_init());
  temp_dir scratch;
  CHECK(static_cast<bool>(scratch));

  char served[PATH_MAX], crls[PATH_MAX], url[256], one[PATH_MAX], two[PATH_MAX];
  path_in(served, "served");
  CHECK_EQ(make_dir(served), 0);
  join_path(crls, sizeof(crls), served, "cli.crls");
  make_file("one.bin", 2000, 41);
  make_file("two.bin", 3000, 42);
  path_in(one, "one.bin");
  path_in(two, "two.bin");

  archive a;
  CHECK(a.create(crls, PASSWORD, 1, 8u << 20));
  a.close();

  test_server server(served);
  int port = server.port();
  CHECK(port > 0);
  snprintf(url, sizeof(url), "http://127.0.0.1:%d/cli.crls", port);

  /* argv is writable in C and C++, string literals are not. */
  char upload[] = "--upload";
  char *usage[] = {upload, url};
  CHECK_EQ(cli_upload(usage), 2);

  /* The wrong password opens nothing and uploads nothing. */
  char *args[] = {upload, url, one, two};
  int code = run_upload("not-the-password", args);
  if(strstr(status_text, "libcurl") != NULL){
    printf("SKIP: %s\n", status_text);
    return SKIP;
  }
  CHECK_EQ(code, 1);

  CHECK(run_upload(PASSWORD, args) == 0);
  /* Uploading the same names again fails for each file. */
  CHECK(run_upload(PASSWORD, args) == 1);

  CHECK(a.open(crls, PASSWORD));
  CHECK_EQ(a.entries().size(), 2);
  if(a.entries().size() == 2){
    CHECK_EQ(a.entries()[0].name, "one.bin");
    CHECK_EQ(a.entries()[1].name, "two.bin");
  }
  a.close();

  return test_summary();
}
