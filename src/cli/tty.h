/**
 * @file    tty.h
 * @brief   Operating system abstraction for reading a password from the user.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * | File          | Platform                                       |
 * |---------------|------------------------------------------------|
 * | tty_posix.c   | termios on /dev/tty                            |
 * | tty_win32.c   | console mode without ENABLE_ECHO_INPUT         |
 */

#ifndef CLI_TTY_H
#define CLI_TTY_H

#include <span>

namespace crls{

/**
 * @brief  Reads a password without echoing it. When standard input is not a
 *         terminal (a pipe or a file), one line is read from it instead, so
 *         scripts can provide the password.
 * @param  prompt Text shown before reading (on standard error).
 * @param  out    Destination, NUL-terminated on success (use a secret_text).
 * @return True if a line was read (without its line ending), false otherwise.
 */
bool tty_read_password(const char *prompt, std::span<char> out);

}

#endif
