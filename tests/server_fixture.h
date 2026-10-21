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
 * @brief  Starts a server for dir on 127.0.0.1 and a free port.
 * @param  dir Folder to serve.
 * @return The port, or 0 if the server did not start.
 */
int  start_test_server(const char *dir);

/**
 * @brief Stops the server started by start_test_server() and waits for it.
 */
void stop_test_server(void);

#endif
