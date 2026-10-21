/**
 * @file    net_posix.c
 * @brief   POSIX implementation of the socket abstraction (see net.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "core/net.h"

#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>

bool net_init(void){
  return signal(SIGPIPE, SIG_IGN) != SIG_ERR;
}

/**
 * @brief  Returns the port a socket is bound to.
 * @param  s Socket.
 * @return Port, or -1 on error.
 */
static int local_port(int s){
  struct sockaddr_storage addr;
  socklen_t len = sizeof(addr);
  if(getsockname(s, (struct sockaddr *) &addr, &len) != 0) return -1;
  if(addr.ss_family == AF_INET) return ntohs(((struct sockaddr_in *) &addr)->sin_port);
  if(addr.ss_family == AF_INET6) return ntohs(((struct sockaddr_in6 *) &addr)->sin6_port);
  return -1;
}

net_socket net_listen(const char *address, int port, int *bound_port){
  struct addrinfo hints, *list;
  char service[16];
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE | AI_NUMERICHOST | AI_NUMERICSERV;
  snprintf(service, sizeof(service), "%d", port);
  if(getaddrinfo(address, service, &hints, &list) != 0) return NET_INVALID;

  int s = -1;
  for(struct addrinfo *ai = list; ai && s < 0; ai = ai->ai_next){
    s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(s < 0) continue;
    /* Allows restarting the server while old connections are in TIME_WAIT. */
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));
    if(bind(s, ai->ai_addr, ai->ai_addrlen) != 0 || listen(s, 16) != 0){
      close(s);
      s = -1;
    }
  }
  freeaddrinfo(list);
  if(s >= 0 && bound_port) *bound_port = local_port(s);
  return s < 0 ? NET_INVALID : (net_socket) s;
}

net_socket net_accept(net_socket listener){
  int s = accept((int) listener, NULL, NULL);
  return s < 0 ? NET_INVALID : (net_socket) s;
}

net_socket net_connect(const char *host, int port){
  struct addrinfo hints, *list;
  char service[16];
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(service, sizeof(service), "%d", port);
  if(getaddrinfo(host, service, &hints, &list) != 0) return NET_INVALID;

  int s = -1;
  for(struct addrinfo *ai = list; ai && s < 0; ai = ai->ai_next){
    s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(s >= 0 && connect(s, ai->ai_addr, ai->ai_addrlen) != 0){
      close(s);
      s = -1;
    }
  }
  freeaddrinfo(list);
  return s < 0 ? NET_INVALID : (net_socket) s;
}

bool net_set_timeout(net_socket s, int seconds){
  struct timeval tv = {seconds, 0};
  return setsockopt((int) s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0 &&
         setsockopt((int) s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) == 0;
}

long net_recv(net_socket s, void *buf, size_t len){
  ssize_t n = recv((int) s, buf, len, 0);
  return n < 0 ? -1 : (long) n;
}

bool net_send_all(net_socket s, const void *buf, size_t len){
  const char *p = (const char *) buf;
  while(len > 0){
    ssize_t n = send((int) s, p, len, MSG_NOSIGNAL);
    if(n <= 0) return false;
    p += n;
    len -= (size_t) n;
  }
  return true;
}

void net_close(net_socket s){
  if(s != NET_INVALID) close((int) s);
}
