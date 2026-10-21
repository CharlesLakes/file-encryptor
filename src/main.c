/**
 * @file    main.c
 * @brief   Program entry point: initializes libsodium and the window, then
 *          runs the event loop.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * Refer to README.md for the module overview and to ui/ui.h for the event flow.
 */

#include "core/archive.h"
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

int main(void){
  if(!archive_crypto_init()){
    platform_error("Could not initialize libsodium.");
    return 1;
  }

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
