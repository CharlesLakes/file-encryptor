/**
 * @file    status.h
 * @brief   Status line: the single message displayed at the bottom of the window.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section status_overview Overview
 *
 * Any module may report success or failure by calling set_status(). Functions
 * that return false always set an error status beforehand, so callers only
 * need to check the return value.
 *
 * The message lives in a fixed buffer on purpose: set_status() is often called
 * right after something failed, possibly an allocation, so it must not
 * allocate memory itself. Longer messages are truncated.
 */

#ifndef CORE_STATUS_H
#define CORE_STATUS_H

namespace crls{

/**
 * @brief Lets the compiler check the arguments of a printf-style function
 *        (MinGW uses its own printf, hence gnu_printf there).
 */
#if defined(__MINGW32__)
#define CRLS_PRINTF(format_index, first_arg) __attribute__((format(gnu_printf, format_index, first_arg)))
#elif defined(__GNUC__)
#define CRLS_PRINTF(format_index, first_arg) __attribute__((format(printf, format_index, first_arg)))
#else
#define CRLS_PRINTF(format_index, first_arg)
#endif

/**
 * @brief Sets the status message using a printf-style format.
 * @param error  True to display the message as an error (in red).
 * @param format printf-style format string, followed by its arguments.
 */
void set_status(bool error, const char *format, ...) CRLS_PRINTF(2, 3);

/**
 * @brief Clears the status message.
 */
void clear_status();

/**
 * @brief  Returns the current status message.
 * @return NUL-terminated text, empty if there is none. It stays valid until
 *         the next set_status() or clear_status().
 */
const char* status_message() noexcept;

/** @brief True if the current status message is an error. */
bool status_is_error() noexcept;

}

#endif
