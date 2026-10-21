/**
 * @file    tty_win32.cpp
 * @brief   Windows implementation of the password prompt (see tty.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "cli/tty.h"

#include <algorithm>
#include <cstdio>
#include <string_view>
#include <windows.h>

namespace crls{

/**
 * @brief Turns off the echo of a console, and turns it back on when it goes
 *        out of scope (also on early returns).
 */
class echo_off{
public:
  /** @brief Turns off the echo of the console behind in, if it is one. */
  explicit echo_off(HANDLE in) noexcept : in_(in), active_(GetConsoleMode(in, &saved_) != 0){
    if(active_) SetConsoleMode(in_, saved_ & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
  }

  ~echo_off(){ if(active_) SetConsoleMode(in_, saved_); }

  echo_off(const echo_off &) = delete;
  echo_off& operator=(const echo_off &) = delete;

  /** @brief True if the input is a console (not a pipe or a file). */
  bool is_console() const noexcept{ return active_; }

private:
  HANDLE in_;
  DWORD saved_ = 0;
  bool active_;
};

bool tty_read_password(const char *prompt, std::span<char> out){
  bool ok = false, console = false;
  {
    /* Not a console (a pipe or a file): one line is read as it is. */
    echo_off quiet(GetStdHandle(STD_INPUT_HANDLE));
    console = quiet.is_console();
    if(console){
      std::fputs(prompt, stderr);
      std::fflush(stderr);
    }
    ok = fgets(out.data(), static_cast<int>(out.size()), stdin) != nullptr;
  }
  if(console) std::fputc('\n', stderr);
  if(ok){
    std::string_view line(out.data());
    out[std::min(line.find_first_of("\r\n"), line.size())] = '\0';
  }
  return ok;
}

}
