/**
 * @file    main.c
 * @brief   Program entry point: initializes libsodium and the window, then
 *          runs the X11 event loop.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * Refer to README.md for the module overview and to ui/ui.h for the event flow.
 */

#include <stdio.h>
#include "core/archive.h"
#include "ui/ui.h"

int main(void){
  if(!archive_crypto_init()){
    fprintf(stderr, "Could not initialize libsodium.\n");
    return 1;
  }

  app_init();
  if(!ui_open_window()){
    app_free();
    return 1;
  }

  bool running = true;
  while(running){
    XEvent event;
    XNextEvent(gui.dpy, &event);

    if(event.type == ButtonPress) handle_click(&event.xbutton);
    else if(event.type == KeyPress) handle_key(&event.xkey);
    else if(event.type == ClientMessage && (Atom) event.xclient.data.l[0] == gui.wm_delete) running = false;

    if(running && (event.type == Expose || event.type == ButtonPress || event.type == KeyPress)) draw();
  }

  app_free();
  ui_close_window();
  return 0;
}
