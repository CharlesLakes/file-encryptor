/**
 * @file    server_fixture.hpp
 * @brief   Runs the HTTP server in a background thread for the tests.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#ifndef TESTS_SERVER_FIXTURE_HPP
#define TESTS_SERVER_FIXTURE_HPP

#include <atomic>
#include <string>
#include <thread>
#include "server/server.hpp"

/**
 * @brief A server for one folder on 127.0.0.1 and a free port, running in a
 *        background thread from construction to destruction.
 */
class test_server{
public:
  /** @brief Starts serving dir; port() is 0 if the server did not start. */
  explicit test_server(const std::string &dir);

  /** @brief Stops the server and waits for its thread. */
  ~test_server();

  test_server(const test_server &) = delete;
  test_server& operator=(const test_server &) = delete;

  /** @brief The port the server listens on, or 0. */
  int port() const noexcept{ return port_; }

private:
  crls::server_config cfg_;
  std::atomic<bool> stop_{false};
  int port_ = 0;
  std::thread thread_;
};

#endif
