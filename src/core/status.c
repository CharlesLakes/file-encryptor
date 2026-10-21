/**
 * @file    status.c
 * @brief   Implementation of the status line (see status.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/status.h"

#include <stdarg.h>
#include <stdio.h>

char status_text[640] = "";
bool status_error = false;

void set_status(bool error, const char *format, ...){
  va_list args;
  va_start(args, format);
  vsnprintf(status_text, sizeof(status_text), format, args);
  va_end(args);
  status_error = error;
}
