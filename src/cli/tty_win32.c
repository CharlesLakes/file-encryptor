/**
 * @file    tty_win32.c
 * @brief   Windows implementation of the password prompt (see tty.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "cli/tty.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

bool tty_read_password(const char *prompt, char *out, size_t out_size){
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  /* Not a console (a pipe or a file): read one line as it is. */
  bool console = GetConsoleMode(in, &mode) != 0;
  if(console){
    SetConsoleMode(in, mode & ~(DWORD) ENABLE_ECHO_INPUT);
    fputs(prompt, stderr);
    fflush(stderr);
  }
  bool ok = fgets(out, (int) out_size, stdin) != NULL;
  if(console){
    SetConsoleMode(in, mode);
    fputc('\n', stderr);
  }
  if(ok) out[strcspn(out, "\r\n")] = '\0';
  return ok;
}
