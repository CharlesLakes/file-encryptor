/**
 * @file    platform_x11.c
 * @brief   Xlib implementation of the windowing system abstraction (see platform.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * Colors are passed to the X server as pixel values, which assumes a 24-bit
 * TrueColor visual (the default on current Linux desktops).
 */

#include "ui/platform.h"

#include <stdio.h>
#include "deps/xlib.h"

/** @brief X11 resources of the window. */
static struct{
  Display *dpy;                  /**< Connection to the X server. */
  Window win;                    /**< Main window. */
  Atom wm_delete;                /**< WM_DELETE_WINDOW protocol atom. */
  Pixmap buffer;                 /**< Off-screen buffer. */
  GC gc;                         /**< Graphics context. */
  int width, height;             /**< Size of the buffer. */
  unsigned int depth;            /**< Color depth of the screen. */
  XFontStruct *fonts[FONT_COUNT];/**< Loaded fonts, indexed by FONT_*. */
}x11;

/**
 * @brief  Loads the first available font from a list.
 * @param  names NULL-terminated list of font names, in order of preference.
 * @return The loaded font, or NULL if none is available.
 */
static XFontStruct* load_font(const char **names){
  for(int i = 0; names[i]; i++){
    XFontStruct *f = XLoadQueryFont(x11.dpy, names[i]);
    if(f) return f;
  }
  return NULL;
}

bool platform_open(int width, int height, const char *title, unsigned long background){
  x11.dpy = XOpenDisplay(NULL);
  if(!x11.dpy){
    platform_error("Could not open the X11 display.");
    return false;
  }

  const char *names[FONT_COUNT][4] = {
    {"9x15", "fixed", NULL},
    {"9x15bold", "9x15", "fixed", NULL},
    {"10x20", "9x15bold", "fixed", NULL},
  };
  for(int i = 0; i < FONT_COUNT; i++){
    x11.fonts[i] = load_font(names[i]);
    if(!x11.fonts[i]){
      platform_error("Could not load an X11 font.");
      XCloseDisplay(x11.dpy);
      return false;
    }
  }

  int scr = XDefaultScreen(x11.dpy);
  x11.width = width;
  x11.height = height;
  x11.depth = (unsigned int) XDefaultDepth(x11.dpy, scr);
  x11.win = XCreateSimpleWindow(x11.dpy, XRootWindow(x11.dpy, scr), 0, 0, (unsigned int) width,
                                (unsigned int) height, 0, 0, background);
  XStoreName(x11.dpy, x11.win, title);
  XSelectInput(x11.dpy, x11.win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);

  XSizeHints hints = {0};
  hints.flags = PMinSize;
  hints.min_width = width;
  hints.min_height = height;
  XSetWMNormalHints(x11.dpy, x11.win, &hints);

  x11.wm_delete = XInternAtom(x11.dpy, "WM_DELETE_WINDOW", 0);
  XSetWMProtocols(x11.dpy, x11.win, &x11.wm_delete, 1);
  x11.buffer = XCreatePixmap(x11.dpy, x11.win, (unsigned int) width, (unsigned int) height, x11.depth);
  x11.gc = XCreateGC(x11.dpy, x11.win, 0, NULL);
  XMapWindow(x11.dpy, x11.win);
  return true;
}

void platform_set_redraw(void (*redraw)(void)){
  /* X11 always returns events, so main() repaints after EVENT_RESIZE. */
  (void) redraw;
}

int platform_width(void){
  return x11.width;
}

int platform_height(void){
  return x11.height;
}

void platform_close(void){
  for(int i = 0; i < FONT_COUNT; i++) XFreeFont(x11.dpy, x11.fonts[i]);
  XFreePixmap(x11.dpy, x11.buffer);
  XFreeGC(x11.dpy, x11.gc);
  XDestroyWindow(x11.dpy, x11.win);
  XCloseDisplay(x11.dpy);
}

void platform_error(const char *message){
  fprintf(stderr, "%s\n", message);
}

/**
 * @brief  Translates an X11 key symbol into a KEY_* value.
 * @param  sym Key symbol.
 * @return KEY_* value, or KEY_NONE if the key is not used.
 */
static int translate_key(KeySym sym){
  switch(sym){
    case XK_Up:          return KEY_UP;
    case XK_Down:        return KEY_DOWN;
    case XK_Page_Up:     return KEY_PAGE_UP;
    case XK_Page_Down:   return KEY_PAGE_DOWN;
    case XK_Home:        return KEY_HOME;
    case XK_End:         return KEY_END;
    case XK_Tab:         return KEY_TAB;
    case XK_ISO_Left_Tab:return KEY_BACK_TAB;
    case XK_Return:
    case XK_KP_Enter:    return KEY_ENTER;
    case XK_Escape:      return KEY_ESCAPE;
    case XK_BackSpace:   return KEY_BACKSPACE;
    case XK_Delete:      return KEY_DELETE;
  }
  return KEY_NONE;
}

void platform_next_event(ui_event *event){
  XEvent xe;
  XNextEvent(x11.dpy, &xe);
  event->type = EVENT_NONE;

  if(xe.type == Expose){
    event->type = EVENT_EXPOSE;
  }else if(xe.type == ConfigureNotify){
    /* Also sent when the window only moves. */
    int w = xe.xconfigure.width, h = xe.xconfigure.height;
    if(w != x11.width || h != x11.height){
      XFreePixmap(x11.dpy, x11.buffer);
      x11.buffer = XCreatePixmap(x11.dpy, x11.win, (unsigned int) w, (unsigned int) h, x11.depth);
      x11.width = w;
      x11.height = h;
      event->type = EVENT_RESIZE;
    }
  }else if(xe.type == ButtonPress){
    event->x = xe.xbutton.x;
    event->y = xe.xbutton.y;
    event->time = xe.xbutton.time;
    /* Buttons 4 and 5 correspond to the mouse wheel. */
    if(xe.xbutton.button == 1) event->type = EVENT_CLICK;
    else if(xe.xbutton.button == 4 || xe.xbutton.button == 5){
      event->type = EVENT_WHEEL;
      event->wheel = xe.xbutton.button == 4 ? -1 : 1;
    }
  }else if(xe.type == KeyPress){
    char text[8];
    KeySym sym;
    int n = XLookupString(&xe.xkey, text, sizeof(text), &sym, NULL);
    event->key = translate_key(sym);
    if(event->key == KEY_NONE && n == 1 && text[0] >= 32 && text[0] < 127){
      event->key = KEY_CHAR;
      event->ch = text[0];
    }
    if(event->key != KEY_NONE) event->type = EVENT_KEY;
  }else if(xe.type == ClientMessage && (Atom) xe.xclient.data.l[0] == x11.wm_delete){
    event->type = EVENT_QUIT;
  }
}

void platform_fill_rect(int x, int y, int w, int h, unsigned long color){
  XSetForeground(x11.dpy, x11.gc, color);
  XFillRectangle(x11.dpy, x11.buffer, x11.gc, x, y, (unsigned int) w, (unsigned int) h);
}

void platform_draw_rect(int x, int y, int w, int h, unsigned long color){
  XSetForeground(x11.dpy, x11.gc, color);
  XDrawRectangle(x11.dpy, x11.buffer, x11.gc, x, y, (unsigned int) w - 1, (unsigned int) h - 1);
}

void platform_draw_text(int font, int x, int baseline, const char *s, int len, unsigned long color){
  XSetForeground(x11.dpy, x11.gc, color);
  XSetFont(x11.dpy, x11.gc, x11.fonts[font]->fid);
  XDrawString(x11.dpy, x11.buffer, x11.gc, x, baseline, s, len);
}

int platform_text_width(int font, const char *s, int len){
  return XTextWidth(x11.fonts[font], s, len);
}

int platform_font_ascent(int font){
  return x11.fonts[font]->ascent;
}

int platform_font_descent(int font){
  return x11.fonts[font]->descent;
}

void platform_present(void){
  XCopyArea(x11.dpy, x11.buffer, x11.win, x11.gc, 0, 0, (unsigned int) x11.width, (unsigned int) x11.height, 0, 0);
  XFlush(x11.dpy);
}
