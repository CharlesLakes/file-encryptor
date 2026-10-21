/**
 * @file    net_win32.cpp
 * @brief   Winsock implementation of the socket abstraction (see net.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * SO_EXCLUSIVEADDRUSE is used instead of SO_REUSEADDR, which on Windows would
 * let another process bind the same port and steal connections.
 */

#include "core/net.h"

#include <algorithm>
#include <memory>
#include <winsock2.h>
#include <ws2tcpip.h>
#include "lib/function_ref.h"

namespace crls{

/** @brief Largest transfer passed to a single recv() or send() call. */
inline constexpr std::size_t MAX_TRANSFER = 1u << 30;

bool net_init(){
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

/** @brief Deleter of an address list from getaddrinfo(). */
struct addrinfo_deleter{
  void operator()(addrinfo *list) const noexcept{ freeaddrinfo(list); }
};

/**
 * @brief  Returns the Winsock handle behind a socket.
 * @param  s Socket.
 * @return The SOCKET.
 */
static SOCKET to_socket(net_socket s){
  return static_cast<SOCKET>(s);
}

/**
 * @brief  Returns the port a socket is bound to.
 * @param  s Socket.
 * @return Port, or -1 on error.
 */
static int local_port(SOCKET s){
  sockaddr_storage addr;
  int len = sizeof(addr);
  if(getsockname(s, reinterpret_cast<sockaddr *>(&addr), &len) != 0) return -1;
  if(addr.ss_family == AF_INET) return ntohs(reinterpret_cast<const sockaddr_in *>(&addr)->sin_port);
  if(addr.ss_family == AF_INET6) return ntohs(reinterpret_cast<const sockaddr_in6 *>(&addr)->sin6_port);
  return -1;
}

/**
 * @brief  Resolves host and port and returns the first address for which a
 *         socket can be created and set up.
 * @param  host  Host name or address (nullptr for any, with AI_PASSIVE).
 * @param  port  Port.
 * @param  flags getaddrinfo() flags.
 * @param  setup Binds or connects the socket; returns false to try the next
 *               address.
 * @return The socket, empty if no address worked.
 */
static unique_socket first_socket(const char *host, int port, int flags,
                                  function_ref<bool(SOCKET s, const addrinfo &ai)> setup){
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = flags;
  addrinfo *found = nullptr;
  if(getaddrinfo(host, std::to_string(port).c_str(), &hints, &found) != 0) return unique_socket();
  std::unique_ptr<addrinfo, addrinfo_deleter> list(found);

  for(const addrinfo *ai = list.get(); ai; ai = ai->ai_next){
    SOCKET s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(s == INVALID_SOCKET) continue;
    unique_socket owned(static_cast<net_socket>(s));
    if(setup(s, *ai)) return owned;
  }
  return unique_socket();
}

net_listener net_listen(const std::string &address, int port){
  net_listener out;
  out.socket = first_socket(address.c_str(), port, AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV,
                            [](SOCKET s, const addrinfo &ai){
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char *>(&on), sizeof(on));
    return bind(s, ai.ai_addr, static_cast<int>(ai.ai_addrlen)) == 0 && listen(s, 16) == 0;
  });
  if(out.socket) out.port = local_port(to_socket(out.socket.get()));
  return out;
}

unique_socket net_accept(net_socket listener){
  SOCKET s = accept(to_socket(listener), nullptr, nullptr);
  return unique_socket(s == INVALID_SOCKET ? NET_INVALID : static_cast<net_socket>(s));
}

unique_socket net_connect(const std::string &host, int port){
  return first_socket(host.c_str(), port, 0, [](SOCKET s, const addrinfo &ai){
    return connect(s, ai.ai_addr, static_cast<int>(ai.ai_addrlen)) == 0;
  });
}

bool net_set_timeout(net_socket s, int seconds){
  /* Winsock takes the timeout in milliseconds, as a DWORD. */
  DWORD ms = static_cast<DWORD>(seconds) * 1000;
  const char *value = reinterpret_cast<const char *>(&ms);
  return setsockopt(to_socket(s), SOL_SOCKET, SO_RCVTIMEO, value, sizeof(ms)) == 0 &&
         setsockopt(to_socket(s), SOL_SOCKET, SO_SNDTIMEO, value, sizeof(ms)) == 0;
}

std::optional<std::size_t> net_recv(net_socket s, std::span<unsigned char> buf){
  int want = static_cast<int>(std::min(buf.size(), MAX_TRANSFER));
  int n = recv(to_socket(s), reinterpret_cast<char *>(buf.data()), want, 0);
  if(n < 0) return std::nullopt;
  return static_cast<std::size_t>(n);
}

bool net_send_all(net_socket s, std::span<const unsigned char> buf){
  while(!buf.empty()){
    int want = static_cast<int>(std::min(buf.size(), MAX_TRANSFER));
    int n = send(to_socket(s), reinterpret_cast<const char *>(buf.data()), want, 0);
    if(n <= 0) return false;
    buf = buf.subspan(static_cast<std::size_t>(n));
  }
  return true;
}

void net_close(net_socket s){
  if(s != NET_INVALID) closesocket(to_socket(s));
}

}
