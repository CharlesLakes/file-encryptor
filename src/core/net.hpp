/**
 * @file    net.hpp
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
 * | net_posix.cpp | Linux and other POSIX |
 * | net_win32.cpp | Windows (Winsock 2)   |
 */

#ifndef CORE_NET_HPP
#define CORE_NET_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace crls{

/** @brief A socket: a descriptor on POSIX, a SOCKET on Windows. */
using net_socket = intptr_t;

inline constexpr net_socket NET_INVALID = -1;   /**< Value of an invalid socket. */

/**
 * @brief Closes a socket. Safe on NET_INVALID.
 * @param s Socket.
 */
void       net_close(net_socket s);

/**
 * @brief Owns a socket and closes it on destruction (move-only).
 */
class unique_socket{
public:
  unique_socket() = default;

  /** @brief Takes ownership of s, which may be NET_INVALID. */
  explicit unique_socket(net_socket s) noexcept : s_(s) {}

  ~unique_socket(){ net_close(s_); }

  unique_socket(const unique_socket &) = delete;
  unique_socket& operator=(const unique_socket &) = delete;

  unique_socket(unique_socket &&other) noexcept : s_(std::exchange(other.s_, NET_INVALID)) {}

  unique_socket& operator=(unique_socket &&other) noexcept{
    if(this != &other) reset(std::exchange(other.s_, NET_INVALID));
    return *this;
  }

  /** @brief Returns the socket without giving up ownership. */
  net_socket get() const noexcept{ return s_; }

  /**
   * @brief Closes the owned socket and takes ownership of s instead.
   * @param s New socket, NET_INVALID by default.
   */
  void reset(net_socket s = NET_INVALID) noexcept{
    net_close(s_);
    s_ = s;
  }

  /** @brief True if a socket is owned. */
  explicit operator bool() const noexcept{ return s_ != NET_INVALID; }

private:
  net_socket s_ = NET_INVALID;
};

/** @brief A listening socket and the port it is bound to. */
struct net_listener{
  unique_socket socket;      /**< Listening socket, empty on error. */
  int port = 0;              /**< Port actually bound. */
};

/**
 * @brief  Prepares the socket library (Winsock startup; ignoring SIGPIPE on
 *         POSIX, so that a client closing early cannot kill the process).
 * @return True on success, false otherwise.
 */
bool       net_init();

/**
 * @brief  Creates a listening socket.
 * @param  address Address to bind, e.g. "127.0.0.1", "0.0.0.0" or "::".
 * @param  port    Port, or 0 to let the system choose one.
 * @return The socket and its port; the socket is empty on error.
 */
net_listener net_listen(const std::string &address, int port);

/**
 * @brief  Waits for the next connection.
 * @param  listener Listening socket.
 * @return Connected socket, empty on error.
 */
unique_socket net_accept(net_socket listener);

/**
 * @brief  Connects to a TCP server.
 * @param  host Host name or address.
 * @param  port Port.
 * @return Connected socket, empty on error.
 */
unique_socket net_connect(const std::string &host, int port);

/**
 * @brief  Sets the receive and send timeouts of a socket.
 * @param  s       Socket.
 * @param  seconds Timeout in seconds.
 * @return True on success, false otherwise.
 */
bool       net_set_timeout(net_socket s, int seconds);

/**
 * @brief  Receives up to buf.size() bytes.
 * @param  s   Socket.
 * @param  buf Destination.
 * @return Number of bytes received (0 if the peer closed the connection), or
 *         std::nullopt on error or timeout.
 */
std::optional<std::size_t> net_recv(net_socket s, std::span<unsigned char> buf);

/** @brief net_recv() into characters, e.g. the head of an HTTP message. */
inline std::optional<std::size_t> net_recv(net_socket s, std::span<char> buf){
  return net_recv(s, std::span<unsigned char>(reinterpret_cast<unsigned char *>(buf.data()), buf.size()));
}

/**
 * @brief  Sends all of buf.
 * @param  s   Socket.
 * @param  buf Bytes to send.
 * @return True on success, false on error, timeout or closed connection.
 */
bool       net_send_all(net_socket s, std::span<const unsigned char> buf);

/** @brief net_send_all() of text, e.g. HTTP headers. */
inline bool net_send_all(net_socket s, std::string_view text){
  return net_send_all(s, std::span<const unsigned char>(reinterpret_cast<const unsigned char *>(text.data()), text.size()));
}

}

#endif
