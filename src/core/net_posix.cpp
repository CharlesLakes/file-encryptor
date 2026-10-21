/**
 * @file    net_posix.cpp
 * @brief   POSIX implementation of the socket abstraction (see net.hpp).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/net.hpp"

#include <csignal>
#include <memory>
#include <netdb.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include "lib/function_ref.hpp"
#include "lib/unique_fd.hpp"

namespace crls{

bool net_init(){
  return std::signal(SIGPIPE, SIG_IGN) != SIG_ERR;
}

/** @brief Deleter of an address list from getaddrinfo(). */
struct addrinfo_deleter{
  void operator()(addrinfo *list) const noexcept{ freeaddrinfo(list); }
};

/**
 * @brief  Returns the port a socket is bound to.
 * @param  s Socket.
 * @return Port, or -1 on error.
 */
static int local_port(int s){
  sockaddr_storage addr;
  socklen_t len = sizeof(addr);
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
                                  function_ref<bool(int s, const addrinfo &ai)> setup){
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = flags;
  addrinfo *found = nullptr;
  if(getaddrinfo(host, std::to_string(port).c_str(), &hints, &found) != 0) return unique_socket();
  std::unique_ptr<addrinfo, addrinfo_deleter> list(found);

  for(const addrinfo *ai = list.get(); ai; ai = ai->ai_next){
    unique_fd s(socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
    if(s && setup(s.get(), *ai)) return unique_socket(s.release());
  }
  return unique_socket();
}

net_listener net_listen(const std::string &address, int port){
  net_listener out;
  out.socket = first_socket(address.c_str(), port, AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV,
                            [](int s, const addrinfo &ai){
    /* Allows restarting the server while old connections are in TIME_WAIT. */
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    return bind(s, ai.ai_addr, ai.ai_addrlen) == 0 && listen(s, 16) == 0;
  });
  if(out.socket) out.port = local_port(static_cast<int>(out.socket.get()));
  return out;
}

unique_socket net_accept(net_socket listener){
  int s = accept(static_cast<int>(listener), nullptr, nullptr);
  return unique_socket(s < 0 ? NET_INVALID : s);
}

unique_socket net_connect(const std::string &host, int port){
  return first_socket(host.c_str(), port, 0, [](int s, const addrinfo &ai){
    return connect(s, ai.ai_addr, ai.ai_addrlen) == 0;
  });
}

bool net_set_timeout(net_socket s, int seconds){
  timeval tv{};
  tv.tv_sec = seconds;
  int fd = static_cast<int>(s);
  return setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
         setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

std::optional<std::size_t> net_recv(net_socket s, std::span<unsigned char> buf){
  ssize_t n = recv(static_cast<int>(s), buf.data(), buf.size(), 0);
  if(n < 0) return std::nullopt;
  return static_cast<std::size_t>(n);
}

bool net_send_all(net_socket s, std::span<const unsigned char> buf){
  while(!buf.empty()){
    ssize_t n = send(static_cast<int>(s), buf.data(), buf.size(), MSG_NOSIGNAL);
    if(n <= 0) return false;
    buf = buf.subspan(static_cast<std::size_t>(n));
  }
  return true;
}

void net_close(net_socket s){
  if(s != NET_INVALID) close(static_cast<int>(s));
}

}
