/**
 * @file    net.h
 * @brief   Operating system abstraction for TCP sockets.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section net_overview Overview
 *
 * Blocking TCP sockets with timeouts, used by the server and by the tests.
 * The implementation is selected at build time:
 *
 * | File          | Platform              |
 * |---------------|-----------------------|
 * | net_posix.c   | Linux and other POSIX |
 * | net_win32.c   | Windows (Winsock 2)   |
 */

#ifndef CORE_NET_H
#define CORE_NET_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

namespace crls{

/** @brief A socket: a descriptor on POSIX, a SOCKET on Windows. */
typedef intptr_t net_socket;

#define NET_INVALID ((net_socket) -1)   /**< Value of an invalid socket. */

/**
 * @brief  Prepares the socket library (Winsock startup; ignoring SIGPIPE on
 *         POSIX, so that a client closing early cannot kill the process).
 * @return True on success, false otherwise.
 */
bool       net_init(void);

/**
 * @brief  Creates a listening socket.
 * @param  address    Address to bind, e.g. "127.0.0.1", "0.0.0.0" or "::".
 * @param  port       Port, or 0 to let the system choose one.
 * @param  bound_port Output: the port actually bound (may be NULL).
 * @return Listening socket, or NET_INVALID on error.
 */
net_socket net_listen(const char *address, int port, int *bound_port);

/**
 * @brief  Waits for the next connection.
 * @param  listener Listening socket.
 * @return Connected socket, or NET_INVALID on error.
 */
net_socket net_accept(net_socket listener);

/**
 * @brief  Connects to a TCP server.
 * @param  host Host name or address.
 * @param  port Port.
 * @return Connected socket, or NET_INVALID on error.
 */
net_socket net_connect(const char *host, int port);

/**
 * @brief  Sets the receive and send timeouts of a socket.
 * @param  s       Socket.
 * @param  seconds Timeout in seconds.
 * @return True on success, false otherwise.
 */
bool       net_set_timeout(net_socket s, int seconds);

/**
 * @brief  Receives up to len bytes.
 * @param  s   Socket.
 * @param  buf Destination buffer.
 * @param  len Capacity of buf.
 * @return Number of bytes received, 0 if the peer closed the connection, or
 *         -1 on error or timeout.
 */
long       net_recv(net_socket s, void *buf, size_t len);

/**
 * @brief  Sends exactly len bytes.
 * @param  s   Socket.
 * @param  buf Bytes to send.
 * @param  len Number of bytes.
 * @return True on success, false on error, timeout or closed connection.
 */
bool       net_send_all(net_socket s, const void *buf, size_t len);

/**
 * @brief Closes a socket. Safe on NET_INVALID.
 * @param s Socket.
 */
void       net_close(net_socket s);

}

#endif
