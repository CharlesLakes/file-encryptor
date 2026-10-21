/**
 * @file    main.cpp
 * @brief   Program entry point: runs the GUI, or a command-line mode when
 *          arguments are given.
 *
 * | Command                                  | Mode                          |
 * |------------------------------------------|-------------------------------|
 * | file-encryptor                           | GUI                           |
 * | file-encryptor --server <folder> [...]   | HTTP server (server/server.h) |
 * | file-encryptor --upload <url> <file>...  | Upload client (cli/cli.h)     |
 *
 * On Windows the GUI executable has no console, so the command-line modes are
 * meant to be run with file-encryptor-cli.exe, which is the same program built
 * as a console application (CRLS_CONSOLE).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * Refer to README.md for the module overview and to ui/ui.h for the event flow.
 */

#include <cstdio>
#include <span>
#include <string_view>
#include "cli/cli.h"
#include "core/archive.h"
#include "server/server.h"
#include "ui/ui.h"

using namespace crls;

/**
 * @brief Adapts the scroll of the current list to the new number of visible
 *        rows and repaints the window.
 * @param app Application state.
 */
static void redraw(app_state &app){
  clamp_scroll(app);
  ensure_visible(app);
  draw(app);
}

/**
 * @brief Prints the command-line usage.
 */
static void usage(){
  fprintf(stderr, "usage: file-encryptor                          start the GUI\n"
                  "       file-encryptor --server <folder>        serve the .crls files of a folder\n"
                  "       file-encryptor --upload <url> <file>... add files to an archive at a URL\n"
                  "Run file-encryptor --server for the server options.\n");
}

/**
 * @brief  Runs a command-line mode.
 * @param  args Arguments, including the program name (at least two).
 * @return Process exit code.
 */
static int run_command([[maybe_unused]] std::span<char *const> args){
#if defined(_WIN32) && !defined(CRLS_CONSOLE)
  /* The GUI executable has no console to print to or read a password from. */
  platform_error("Run the command-line modes with file-encryptor-cli.exe.");
  return 2;
#endif
  std::string_view mode = args[1];
  if(mode == "--server") return server_main(args.subspan(1));
  if(mode == "--upload") return cli_upload(args.subspan(1));
  usage();
  return mode == "--help" ? 0 : 2;
}

int main(int argc, char **argv){
  if(!archive_crypto_init()){
    if(argc > 1) fprintf(stderr, "Could not initialize libsodium.\n");
    else platform_error("Could not initialize libsodium.");
    return 1;
  }
  if(argc > 1) return run_command(std::span<char *const>(argv, static_cast<std::size_t>(argc)));
#ifdef CRLS_CONSOLE
  usage();
  return 2;
#endif

  /* The whole state of the GUI; it lives until the window is closed. */
  app_state app;
  if(!platform_open(MIN_WIDTH, MIN_HEIGHT, ".crls manager", COLOR_BG)) return 1;
  auto repaint = [&app]{ redraw(app); };
  platform_set_redraw(repaint);

  bool running = true;
  while(running){
    ui_event event = platform_next_event();

    if(event.type == ui_event_type::click || event.type == ui_event_type::wheel) handle_click(app, event);
    else if(event.type == ui_event_type::key) handle_key(app, event);
    else if(event.type == ui_event_type::quit) running = false;

    if(event.type == ui_event_type::resize) redraw(app);
    else if(running && event.type != ui_event_type::none) draw(app);
  }

  platform_set_redraw({});
  platform_close();
  return 0;
}
