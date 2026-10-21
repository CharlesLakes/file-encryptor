/**
 * @file    tty_win32.cpp
 * @brief   Windows implementation of the password prompt (see tty.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "cli/tty.h"

#include <stdio.h>
#include <string.h>
#include <windows.h>

namespace crls{

bool tty_read_password(const char *prompt, std::span<char> out){
  HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode;
  /* Not a console (a pipe or a file): read one line as it is. */
  bool console = GetConsoleMode(in, &mode) != 0;
  if(console){
    SetConsoleMode(in, mode & ~(DWORD) ENABLE_ECHO_INPUT);
    fputs(prompt, stderr);
    fflush(stderr);
  }
  bool ok = fgets(out.data(), static_cast<int>(out.size()), stdin) != NULL;
  if(console){
    SetConsoleMode(in, mode);
    fputc('\n', stderr);
  }
  if(ok) out[strcspn(out.data(), "\r\n")] = '\0';
  return ok;
}

}
