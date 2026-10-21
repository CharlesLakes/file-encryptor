/**
 * @file    server_fixture.cpp
 * @brief   Implementation of the background test server (see server_fixture.hpp).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "server_fixture.hpp"

#include <charconv>
#include <chrono>
#include <cstdio>
#include "core/net.hpp"
#include "test_util.hpp"

using namespace crls;

/**
 * @brief  Reads the port written by the server to its port file.
 * @param  path Path of the port file.
 * @return The port, or 0 if the file does not hold one (yet).
 */
static int read_port(const std::string &path){
  std::optional<std::string> text = read_file(path);
  int port = 0;
  if(text) std::from_chars(text->data(), text->data() + text->size(), port);
  return port;
}

test_server::test_server(const std::string &dir){
  net_init();
  std::string port_file = path_in("server.port");
  std::remove(port_file.c_str());
  cfg_.dir = dir;
  cfg_.port = 0;
  cfg_.max_upload = 64ULL << 20;
  cfg_.timeout = 5;
  cfg_.port_file = port_file;
  cfg_.stop = &stop_;
  thread_ = std::thread([this]{ server_run(cfg_); });

  /* Wait up to 4 seconds for the server to listen. */
  for(int i = 0; i < 200 && port_ == 0; i++){
    port_ = read_port(port_file);
    if(port_ == 0) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
}

test_server::~test_server(){
  /* accept() is blocking: set the flag, then wake it up with a connection. */
  stop_ = true;
  unique_socket s = port_ ? net_connect("127.0.0.1", port_) : unique_socket();
  if(!s){
    /* The server never listened (its thread has ended) or cannot be woken:
       do not wait for it forever. */
    thread_.detach();
    return;
  }
  net_send_all(s.get(), "HEAD / HTTP/1.1\r\n\r\n");
  std::array<char, 256> buf;
  while(net_recv(s.get(), std::span(buf)).value_or(0) > 0){}
  thread_.join();
}
