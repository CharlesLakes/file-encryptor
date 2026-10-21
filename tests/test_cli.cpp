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

#include <array>
#include <cstdio>
#include <string>
#include "cli/cli.hpp"
#include "core/archive.hpp"
#include "core/remote.hpp"
#include "core/status.hpp"
#include "core/util.hpp"
#include "server_fixture.hpp"
#include "test_util.hpp"

using namespace crls;

inline constexpr char PASSWORD[] = "cli-password";
inline constexpr int SKIP = 77;   /**< Exit code that CTest reports as skipped. */

/**
 * @brief  Runs cli_upload() with the given password on standard input.
 * @param  password Line given on standard input.
 * @param  args     Arguments, starting at "--upload".
 * @return Exit code of cli_upload(), or -1 if standard input cannot be replaced.
 */
static int run_upload(std::string_view password, std::span<char *const> args){
  std::string input = path_in("stdin.txt");
  if(!write_file(input, std::string(password) + "\n")) return -1;
  if(!freopen(input.c_str(), "r", stdin)) return -1;
  return cli_upload(args);
}

int main(){
  CHECK(archive_crypto_init());
  temp_dir scratch;
  CHECK(static_cast<bool>(scratch));

  std::string served = path_in("served");
  CHECK(make_dir(served));
  std::string crls = join_path(served, "cli.crls");
  make_file("one.bin", 2000, 41);
  make_file("two.bin", 3000, 42);
  std::string one = path_in("one.bin"), two = path_in("two.bin");

  archive a;
  CHECK(a.create(crls, PASSWORD, 1, 8u << 20));
  a.close();

  test_server server(served);
  CHECK(server.port() > 0);
  std::string url = "http://127.0.0.1:" + std::to_string(server.port()) + "/cli.crls";

  /* cli_upload() takes argv, whose strings are writable: std::string::data() is. */
  std::string upload = "--upload";
  std::array<char *, 2> usage = {upload.data(), url.data()};
  CHECK_EQ(cli_upload(usage), 2);

  /* The wrong password opens nothing and uploads nothing. */
  std::array<char *, 4> args = {upload.data(), url.data(), one.data(), two.data()};
  int code = run_upload("not-the-password", args);
  if(status_mentions("libcurl")){
    printf("SKIP: %s\n", status_message());
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
