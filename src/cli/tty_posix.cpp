/**
 * @file    tty_posix.cpp
 * @brief   POSIX implementation of the password prompt (see tty.hpp).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "cli/tty.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string_view>
#include <termios.h>
#include <unistd.h>

namespace crls{

/** @brief Deleter of a FILE. */
struct file_closer{
  void operator()(FILE *f) const noexcept{ fclose(f); }
};

/**
 * @brief Turns off the echo of a terminal, and turns it back on when it goes
 *        out of scope (also on early returns).
 */
class echo_off{
public:
  /** @brief Turns off the echo of the terminal fd, if it is one. */
  explicit echo_off(int fd) noexcept : fd_(fd), active_(tcgetattr(fd, &saved_) == 0){
    if(!active_) return;
    termios quiet = saved_;
    quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO);
    tcsetattr(fd_, TCSAFLUSH, &quiet);
  }

  ~echo_off(){ if(active_) tcsetattr(fd_, TCSAFLUSH, &saved_); }

  echo_off(const echo_off &) = delete;
  echo_off& operator=(const echo_off &) = delete;

private:
  int fd_;
  termios saved_{};
  bool active_;
};

/**
 * @brief  Reads one line from f and removes its line ending.
 * @param  f   Stream.
 * @param  out Destination.
 * @return True if a line was read.
 */
static bool read_line(FILE *f, std::span<char> out){
  if(!fgets(out.data(), static_cast<int>(out.size()), f)) return false;
  std::string_view line(out.data());
  out[std::min(line.find_first_of("\r\n"), line.size())] = '\0';
  return true;
}

bool tty_read_password(const char *prompt, std::span<char> out){
  if(!isatty(STDIN_FILENO)) return read_line(stdin, out);

  std::unique_ptr<FILE, file_closer> tty(fopen("/dev/tty", "r+"));
  if(!tty) return false;
  /* Unbuffered: no copy of the password is left in the buffer of the stream. */
  setvbuf(tty.get(), nullptr, _IONBF, 0);
  bool ok = false;
  {
    echo_off quiet(fileno(tty.get()));
    std::fputs(prompt, stderr);
    std::fflush(stderr);
    ok = read_line(tty.get(), out);
  }
  std::fputc('\n', stderr);
  return ok;
}

}
