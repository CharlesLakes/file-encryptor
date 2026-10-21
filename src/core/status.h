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
 */

#ifndef CORE_STATUS_H
#define CORE_STATUS_H

#include <stdbool.h>

namespace crls{

extern char status_text[640];   /**< Current status message. */
extern bool status_error;       /**< True if the current message is an error. */

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
void clear_status(void);

}

#endif
