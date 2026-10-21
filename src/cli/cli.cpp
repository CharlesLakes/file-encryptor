/**
 * @file    cli.cpp
 * @brief   Implementation of the command-line client (see cli.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "cli/cli.h"

#include <stdio.h>
#include "cli/tty.h"
#include "core/archive.h"
#include "core/remote.h"
#include "core/status.h"
#include "lib/secret.h"

namespace crls{

int cli_upload(std::span<char *const> args){
  if(args.size() < 3 || !remote_is_url(args[1])){
    fprintf(stderr, "usage: file-encryptor --upload <http(s) URL of a .crls> <file> [file ...]\n");
    return 2;
  }

  archive a;
  {
    /* The password only lives in this wiped buffer, and only until the key is derived. */
    secret_text<MAX_PASSWORD + 2> password;
    if(!tty_read_password("Password: ", password.span())){
      fprintf(stderr, "Could not read the password.\n");
      return 2;
    }
    if(!remote_open(a, args[1], password.data())){
      fprintf(stderr, "%s\n", status_message());
      return 1;
    }
  }

  int failed = 0;
  for(char *path : args.subspan(2)){
    bool ok = remote_add_file(a, path);
    fprintf(stderr, "%s: %s\n", path, status_message());
    if(!ok) failed++;
  }
  return failed ? 1 : 0;
}

}
