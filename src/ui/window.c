/**
 * @file    window.c
 * @brief   Creation and destruction of the X11 window and fonts.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.h"

#include <stdio.h>

gui_state gui;

/**
 * @brief  Loads the first available font from a list.
 * @param  names NULL-terminated list of font names, in order of preference.
 * @return The loaded font, or NULL if none is available.
 */
static XFontStruct* load_font(const char **names){
  for(int i = 0; names[i]; i++){
    XFontStruct *f = XLoadQueryFont(gui.dpy, names[i]);
    if(f) return f;
  }
  return NULL;
}

bool ui_open_window(void){
  gui.dpy = XOpenDisplay(NULL);
  if(!gui.dpy){
    fprintf(stderr, "Could not open the X11 display.\n");
    return false;
  }

  const char *font_names[] = {"9x15", "fixed", NULL};
  const char *bold_names[] = {"9x15bold", "9x15", "fixed", NULL};
  const char *title_names[] = {"10x20", "9x15bold", "fixed", NULL};
  gui.font = load_font(font_names);
  gui.bold_font = load_font(bold_names);
  gui.title_font = load_font(title_names);
  if(!gui.font || !gui.bold_font || !gui.title_font){
    fprintf(stderr, "Could not load an X11 font.\n");
    XCloseDisplay(gui.dpy);
    return false;
  }

  int scr = XDefaultScreen(gui.dpy);
  gui.win = XCreateSimpleWindow(gui.dpy, XRootWindow(gui.dpy, scr), 0, 0, WIDTH, HEIGHT, 0, 0, COLOR_BG);
  XStoreName(gui.dpy, gui.win, ".crls manager");
  XSelectInput(gui.dpy, gui.win, ExposureMask | KeyPressMask | ButtonPressMask);
  gui.wm_delete = XInternAtom(gui.dpy, "WM_DELETE_WINDOW", 0);
  XSetWMProtocols(gui.dpy, gui.win, &gui.wm_delete, 1);
  gui.buffer = XCreatePixmap(gui.dpy, gui.win, WIDTH, HEIGHT, (unsigned int) XDefaultDepth(gui.dpy, scr));
  gui.gc = XCreateGC(gui.dpy, gui.win, 0, NULL);
  XMapWindow(gui.dpy, gui.win);
  return true;
}

void ui_close_window(void){
  XFreeFont(gui.dpy, gui.font);
  XFreeFont(gui.dpy, gui.bold_font);
  XFreeFont(gui.dpy, gui.title_font);
  XFreePixmap(gui.dpy, gui.buffer);
  XFreeGC(gui.dpy, gui.gc);
  XDestroyWindow(gui.dpy, gui.win);
  XCloseDisplay(gui.dpy);
}
