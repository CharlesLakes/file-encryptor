/**
 * @file    tty_posix.cpp
 * @brief   POSIX implementation of the password prompt (see tty.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "cli/tty.h"

#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

namespace crls{

/**
 * @brief  Reads one line from f and removes its line ending.
 * @param  f   Stream.
 * @param  out Destination.
 * @return True if a line was read.
 */
static bool read_line(FILE *f, std::span<char> out){
  if(!fgets(out.data(), static_cast<int>(out.size()), f)) return false;
  out[strcspn(out.data(), "\r\n")] = '\0';
  return true;
}

bool tty_read_password(const char *prompt, std::span<char> out){
  if(!isatty(STDIN_FILENO)) return read_line(stdin, out);

  FILE *tty = fopen("/dev/tty", "r+");
  if(!tty) return false;
  struct termios saved, quiet;
  bool restore = tcgetattr(fileno(tty), &saved) == 0;
  if(restore){
    quiet = saved;
    quiet.c_lflag &= ~(tcflag_t) ECHO;
    tcsetattr(fileno(tty), TCSAFLUSH, &quiet);
  }
  fputs(prompt, stderr);
  fflush(stderr);
  bool ok = read_line(tty, out);
  if(restore) tcsetattr(fileno(tty), TCSAFLUSH, &saved);
  fputc('\n', stderr);
  fclose(tty);
  return ok;
}

}
