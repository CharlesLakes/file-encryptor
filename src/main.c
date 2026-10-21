/**
 * @file    main.c
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

#include <stdio.h>
#include <string.h>
#include "cli/cli.h"
#include "core/archive.h"
#include "server/server.h"
#include "ui/ui.h"

/**
 * @brief Adapts the scroll of the current list to the new number of visible
 *        rows and repaints the window.
 */
static void redraw(void){
  clamp_scroll();
  ensure_visible();
  draw();
}

/**
 * @brief Prints the command-line usage.
 */
static void usage(void){
  fprintf(stderr, "usage: file-encryptor                          start the GUI\n"
                  "       file-encryptor --server <folder>        serve the .crls files of a folder\n"
                  "       file-encryptor --upload <url> <file>... add files to an archive at a URL\n"
                  "Run file-encryptor --server for the server options.\n");
}

/**
 * @brief  Runs a command-line mode.
 * @param  argc Number of arguments.
 * @param  argv Arguments.
 * @return Process exit code.
 */
static int run_command(int argc, char **argv){
#if defined(_WIN32) && !defined(CRLS_CONSOLE)
  /* The GUI executable has no console to print to or read a password from. */
  (void) argc;
  (void) argv;
  platform_error("Run the command-line modes with file-encryptor-cli.exe.");
  return 2;
#endif
  if(strcmp(argv[1], "--server") == 0) return server_main(argc - 1, argv + 1);
  if(strcmp(argv[1], "--upload") == 0) return cli_upload(argc - 1, argv + 1);
  usage();
  return strcmp(argv[1], "--help") == 0 ? 0 : 2;
}

int main(int argc, char **argv){
  if(!archive_crypto_init()){
    if(argc > 1) fprintf(stderr, "Could not initialize libsodium.\n");
    else platform_error("Could not initialize libsodium.");
    return 1;
  }
  if(argc > 1) return run_command(argc, argv);
#ifdef CRLS_CONSOLE
  usage();
  return 2;
#endif

  app_init();
  if(!platform_open(MIN_WIDTH, MIN_HEIGHT, ".crls manager", COLOR_BG)){
    app_free();
    return 1;
  }
  platform_set_redraw(redraw);

  bool running = true;
  while(running){
    ui_event event;
    platform_next_event(&event);

    if(event.type == EVENT_CLICK || event.type == EVENT_WHEEL) handle_click(&event);
    else if(event.type == EVENT_KEY) handle_key(&event);
    else if(event.type == EVENT_QUIT) running = false;

    if(event.type == EVENT_RESIZE) redraw();
    else if(running && event.type != EVENT_NONE) draw();
  }

  app_free();
  platform_close();
  return 0;
}
