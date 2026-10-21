/**
 * @file    server_fixture.h
 * @brief   Runs the HTTP server in a background thread for the tests.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#ifndef TESTS_SERVER_FIXTURE_H
#define TESTS_SERVER_FIXTURE_H

/**
 * @brief A server for one folder on 127.0.0.1 and a free port, running in a
 *        background thread from construction to destruction. Only one can
 *        exist at a time.
 */
class test_server{
public:
  /** @brief Starts serving dir; port() is 0 if the server did not start. */
  explicit test_server(const char *dir);

  /** @brief Stops the server and waits for its thread. */
  ~test_server();

  test_server(const test_server &) = delete;
  test_server& operator=(const test_server &) = delete;

  /** @brief The port the server listens on, or 0. */
  int port() const noexcept;
};

#endif
