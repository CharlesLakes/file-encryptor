/**
 * @file    tty_posix.c
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

/**
 * @brief  Reads one line from f and removes its line ending.
 * @param  f        Stream.
 * @param  out      Destination buffer.
 * @param  out_size Capacity of out.
 * @return True if a line was read.
 */
static bool read_line(FILE *f, char *out, size_t out_size){
  if(!fgets(out, (int) out_size, f)) return false;
  out[strcspn(out, "\r\n")] = '\0';
  return true;
}

bool tty_read_password(const char *prompt, char *out, size_t out_size){
  if(!isatty(STDIN_FILENO)) return read_line(stdin, out, out_size);

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
  bool ok = read_line(tty, out, out_size);
  if(restore) tcsetattr(fileno(tty), TCSAFLUSH, &saved);
  fputc('\n', stderr);
  fclose(tty);
  return ok;
}
