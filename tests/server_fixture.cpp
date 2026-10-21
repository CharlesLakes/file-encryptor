/**
 * @file    server_fixture.cpp
 * @brief   Implementation of the background test server (see server_fixture.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "server_fixture.h"

#include <atomic>
#include <stdio.h>
#include <unistd.h>
#include "core/net.h"
#include "server/server.h"
#include "test_util.h"

using namespace crls;

#ifdef _WIN32
#include <windows.h>
static HANDLE thread;
#else
#include <pthread.h>
static pthread_t thread;
#endif

static server_config cfg;
static std::atomic<bool> stop_flag(false);
static char port_file[PATH_MAX];
static int bound_port = 0;

/** @brief Thread body: runs the server until stop_flag is set. */
#ifdef _WIN32
static DWORD WINAPI server_thread(LPVOID arg){
  (void) arg;
  server_run(cfg);
  return 0;
}
#else
static void* server_thread(void *arg){
  (void) arg;
  server_run(cfg);
  return NULL;
}
#endif

test_server::test_server(const char *dir){
  net_init();
  path_in(port_file, "server.port");
  remove(port_file);
  stop_flag = false;
  bound_port = 0;
  cfg = server_config();
  cfg.dir = dir;
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
  for(int i = 0; i < 200 && bound_port == 0; i++){
    FILE *f = fopen(port_file, "r");
    if(f){
      if(fscanf(f, "%d", &bound_port) != 1) bound_port = 0;
      fclose(f);
    }
    if(bound_port == 0) usleep(20000);
  }
}

int test_server::port() const noexcept{
  return bound_port;
}

test_server::~test_server(){
  /* accept() is blocking: set the flag, then wake it up with a connection. */
  stop_flag = true;
  unique_socket s = net_connect("127.0.0.1", bound_port);
  if(s){
    net_send_all(s.get(), "HEAD / HTTP/1.1\r\n\r\n");
    char buf[256];
    while(net_recv(s.get(), std::span(buf)).value_or(0) > 0){}
  }
#ifdef _WIN32
  WaitForSingleObject(thread, INFINITE);
#else
  pthread_join(thread, NULL);
#endif
}
