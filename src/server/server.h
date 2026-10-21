/**
 * @file    server.h
 * @brief   HTTP server that publishes a folder of .crls archives.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section server_overview Overview
 *
 * Started with `file-encryptor --server <folder>`. The server never sees a
 * password or a key: it serves the encrypted bytes as they are and accepts
 * signed appends (see core/append.h).
 *
 * | Request            | Response                                            |
 * |--------------------|-----------------------------------------------------|
 * | GET /              | Names of the .crls files, one per line (text/plain) |
 * | GET, HEAD /x.crls  | The raw archive; single Range requests give 206     |
 * | POST /x.crls       | Signed append (headers below); 204 on success       |
 *
 * Every archive response carries an ETag built from the identity of the file.
 * A client sends it back in If-Match, and gets 412 if the archive was replaced
 * (compacted) in the meantime.
 *
 * A POST carries the body of the append and these headers, all required:
 *
 * | Header              | Content                                  |
 * |---------------------|------------------------------------------|
 * | X-Crls-Base         | Offset where the body goes (decimal)     |
 * | X-Crls-Index-Nonce  | Nonce of the current index (hex)         |
 * | X-Crls-Index-Length | Length of the new index (decimal)        |
 * | X-Crls-Body-Hash    | BLAKE2b-256 of the body (hex)            |
 * | X-Crls-Sig          | Ed25519 signature (hex)                  |
 *
 * Errors: 400 malformed, 403 invalid signature (password not known),
 * 404 unknown archive, 409 the archive changed (reload and retry), 411 no
 * Content-Length, 412 replaced archive, 413 too large, 422 version 2 archive,
 * 423 the archive is open in the GUI, 503 temporarily unreadable (Windows).
 *
 * @section server_model Model
 *
 * One thread, one request per connection (Connection: close). Each receive
 * has a timeout and each request a total deadline, so a slow client cannot
 * hold the server. Only names made of letters, digits, '.', '_', '-' and
 * spaces, ending in .crls and not starting with '.', are served; symbolic
 * links are not followed. The server speaks plain HTTP: put a reverse proxy
 * such as Caddy or nginx in front of it for HTTPS.
 */

#ifndef SERVER_SERVER_H
#define SERVER_SERVER_H

#include <atomic>
#include <cstdint>
#include <string>

namespace crls{

/**
 * @brief Settings of a server.
 */
struct server_config{
  std::string dir;                             /**< Folder with the .crls files (absolute). */
  std::string bind = "127.0.0.1";              /**< Address to listen on. */
  int port = 8080;                             /**< Port, or 0 for any free port. */
  uint64_t max_upload = 1ULL << 30;            /**< Largest accepted POST body in bytes. */
  int timeout = 15;                            /**< Seconds a request may take before its body. */
  std::string port_file;                       /**< If not empty, the bound port is written to this file. */
  const std::atomic<bool> *stop = nullptr;     /**< If set and true, the server stops after the current connection. */
};

/**
 * @brief  Serves requests until cfg.stop becomes true (forever if it is null).
 * @param  cfg Settings.
 * @return Process exit code: 0 after a requested stop, 1 on a startup error.
 */
int server_run(const server_config &cfg);

/**
 * @brief  Parses `--server <folder> [--port N] [--bind ADDR] [--max-upload BYTES]
 *         [--timeout SECONDS] [--port-file PATH]` and runs the server.
 * @param  argc Number of arguments, starting at "--server".
 * @param  argv Arguments, starting at "--server".
 * @return Process exit code.
 */
int server_main(int argc, char **argv);

}

#endif
