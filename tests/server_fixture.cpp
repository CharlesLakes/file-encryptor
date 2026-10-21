/**
 * @file    server_fixture.cpp
 * @brief   Implementation of the background test server (see server_fixture.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "server_fixture.h"

#include <stdio.h>
#include <unistd.h>
#include "core/net.h"
#include "server/server.h"
#include "test_util.h"

#ifdef _WIN32
#include <windows.h>
static HANDLE thread;
#else
#include <pthread.h>
static pthread_t thread;
#endif

static server_config cfg;
static volatile int stop_flag = 0;
static char port_file[PATH_MAX];
static int port = 0;

/** @brief Thread body: runs the server until stop_flag is set. */
#ifdef _WIN32
static DWORD WINAPI server_thread(LPVOID arg){
  (void) arg;
  server_run(&cfg);
  return 0;
}
#else
static void* server_thread(void *arg){
  (void) arg;
  server_run(&cfg);
  return NULL;
}
#endif

int start_test_server(const char *dir){
  net_init();
  path_in(port_file, "server.port");
  remove(port_file);
  stop_flag = 0;
  port = 0;
  cfg.dir = dir;
  cfg.bind = "127.0.0.1";
  cfg.port = 0;
  cfg.max_upload = 64ULL << 20;
  cfg.timeout = 5;
  cfg.port_file = port_file;
  cfg.stop = &stop_flag;
#ifdef _WIN32
  thread = CreateThread(NULL, 0, server_thread, NULL, 0, NULL);
#else
  pthread_create(&thread, NULL, server_thread, NULL);
#endif
  for(int i = 0; i < 200 && port == 0; i++){
    FILE *f = fopen(port_file, "r");
    if(f){
      if(fscanf(f, "%d", &port) != 1) port = 0;
      fclose(f);
    }
    if(port == 0) usleep(20000);
  }
  return port;
}

void stop_test_server(void){
  /* accept() is blocking: set the flag, then wake it up with a connection. */
  stop_flag = 1;
  net_socket s = net_connect("127.0.0.1", port);
  if(s != NET_INVALID){
    net_send_all(s, "HEAD / HTTP/1.1\r\n\r\n", 19);
    char buf[256];
    while(net_recv(s, buf, sizeof(buf)) > 0){}
    net_close(s);
  }
#ifdef _WIN32
  WaitForSingleObject(thread, INFINITE);
#else
  pthread_join(thread, NULL);
#endif
}
