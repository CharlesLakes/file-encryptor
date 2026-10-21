/**
 * @file    cli.c
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
#include "deps/sodium.h"

int cli_upload(int argc, char **argv){
  if(argc < 3 || !remote_is_url(argv[1])){
    fprintf(stderr, "usage: file-encryptor --upload <http(s) URL of a .crls> <file> [file ...]\n");
    return 2;
  }

  char password[MAX_PASSWORD + 2];
  if(!tty_read_password("Password: ", password, sizeof(password))){
    fprintf(stderr, "Could not read the password.\n");
    return 2;
  }

  archive a;
  archive_init(&a);
  bool opened = remote_open(&a, argv[1], password);
  sodium_memzero(password, sizeof(password));
  if(!opened){
    fprintf(stderr, "%s\n", status_text);
    archive_free(&a);
    return 1;
  }

  int failed = 0;
  for(int i = 2; i < argc; i++){
    bool ok = remote_add_file(&a, argv[i]);
    fprintf(stderr, "%s: %s\n", argv[i], status_text);
    if(!ok) failed++;
  }
  archive_free(&a);
  return failed ? 1 : 0;
}
