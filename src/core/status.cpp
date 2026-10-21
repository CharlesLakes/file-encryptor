/**
 * @file    status.cpp
 * @brief   Implementation of the status line (see status.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/status.h"

#include <array>
#include <cstdarg>
#include <cstdio>

namespace crls{

namespace{

std::array<char, 640> status_text{};   /* Current status message. */
bool status_error = false;             /* True if it is an error. */

}

void set_status(bool error, const char *format, ...){
  va_list args;
  va_start(args, format);
  vsnprintf(status_text.data(), status_text.size(), format, args);
  va_end(args);
  status_error = error;
}

void clear_status(){
  status_text[0] = '\0';
  status_error = false;
}

const char* status_message() noexcept{
  return status_text.data();
}

bool status_is_error() noexcept{
  return status_error;
}

}
