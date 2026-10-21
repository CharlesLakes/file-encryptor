/**
 * @file    net_win32.c
 * @brief   Winsock implementation of the socket abstraction (see net.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * SO_EXCLUSIVEADDRUSE is used instead of SO_REUSEADDR, which on Windows would
 * let another process bind the same port and steal connections.
 */

#include "core/net.h"

#include <stdio.h>
#include <string.h>
#include <winsock2.h>
#include <ws2tcpip.h>

bool net_init(void){
  WSADATA data;
  return WSAStartup(MAKEWORD(2, 2), &data) == 0;
}

/**
 * @brief  Returns the port a socket is bound to.
 * @param  s Socket.
 * @return Port, or -1 on error.
 */
static int local_port(SOCKET s){
  struct sockaddr_storage addr;
  int len = sizeof(addr);
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

  SOCKET s = INVALID_SOCKET;
  for(struct addrinfo *ai = list; ai && s == INVALID_SOCKET; ai = ai->ai_next){
    s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(s == INVALID_SOCKET) continue;
    BOOL on = TRUE;
    setsockopt(s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *) &on, sizeof(on));
    if(bind(s, ai->ai_addr, (int) ai->ai_addrlen) != 0 || listen(s, 16) != 0){
      closesocket(s);
      s = INVALID_SOCKET;
    }
  }
  freeaddrinfo(list);
  if(s != INVALID_SOCKET && bound_port) *bound_port = local_port(s);
  return s == INVALID_SOCKET ? NET_INVALID : (net_socket) s;
}

net_socket net_accept(net_socket listener){
  SOCKET s = accept((SOCKET) listener, NULL, NULL);
  return s == INVALID_SOCKET ? NET_INVALID : (net_socket) s;
}

net_socket net_connect(const char *host, int port){
  struct addrinfo hints, *list;
  char service[16];
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  snprintf(service, sizeof(service), "%d", port);
  if(getaddrinfo(host, service, &hints, &list) != 0) return NET_INVALID;

  SOCKET s = INVALID_SOCKET;
  for(struct addrinfo *ai = list; ai && s == INVALID_SOCKET; ai = ai->ai_next){
    s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if(s != INVALID_SOCKET && connect(s, ai->ai_addr, (int) ai->ai_addrlen) != 0){
      closesocket(s);
      s = INVALID_SOCKET;
    }
  }
  freeaddrinfo(list);
  return s == INVALID_SOCKET ? NET_INVALID : (net_socket) s;
}

bool net_set_timeout(net_socket s, int seconds){
  /* Winsock takes the timeout in milliseconds, as a DWORD. */
  DWORD ms = (DWORD) seconds * 1000;
  return setsockopt((SOCKET) s, SOL_SOCKET, SO_RCVTIMEO, (const char *) &ms, sizeof(ms)) == 0 &&
         setsockopt((SOCKET) s, SOL_SOCKET, SO_SNDTIMEO, (const char *) &ms, sizeof(ms)) == 0;
}

long net_recv(net_socket s, void *buf, size_t len){
  int want = len > 1 << 30 ? 1 << 30 : (int) len;
  int n = recv((SOCKET) s, (char *) buf, want, 0);
  return n < 0 ? -1 : (long) n;
}

bool net_send_all(net_socket s, const void *buf, size_t len){
  const char *p = (const char *) buf;
  while(len > 0){
    int want = len > 1 << 30 ? 1 << 30 : (int) len;
    int n = send((SOCKET) s, p, want, 0);
    if(n <= 0) return false;
    p += n;
    len -= (size_t) n;
  }
  return true;
}

void net_close(net_socket s){
  if(s != NET_INVALID) closesocket((SOCKET) s);
}
