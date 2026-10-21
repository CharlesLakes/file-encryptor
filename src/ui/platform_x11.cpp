/**
 * @file    platform_x11.cpp
 * @brief   Xlib implementation of the windowing system abstraction (see platform.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * Colors are passed to the X server as pixel values, which assumes a 24-bit
 * TrueColor visual (the default on current Linux desktops).
 */

#include "ui/platform.h"

#include <cstddef>
#include <stdio.h>
#include <string.h>
#include "deps/xlib.h"

namespace crls{

#if defined(__x86_64__) || defined(__aarch64__)
/* The X11 structures are declared by hand in deps/xlib.h: check them against
   the layout of the real <X11/Xlib.h> on 64-bit Linux. */
static_assert(sizeof(XEvent) == 192, "XEvent must match <X11/Xlib.h>");
static_assert(offsetof(XButtonEvent, x) == 64 && offsetof(XButtonEvent, button) == 84,
              "XButtonEvent must match <X11/Xlib.h>");
static_assert(offsetof(XKeyEvent, keycode) == 84, "XKeyEvent must match <X11/Xlib.h>");
static_assert(offsetof(XConfigureEvent, width) == 56, "XConfigureEvent must match <X11/Xlib.h>");
#endif

namespace{

/**
 * @brief X11 resources of the window. Everything created is released by
 *        release(), also when platform_open() fails halfway.
 */
struct x11_state{
  Display *dpy = nullptr;                    /**< Connection to the X server. */
  Window win = 0;                            /**< Main window. */
  Atom wm_delete = 0;                        /**< WM_DELETE_WINDOW protocol atom. */
  Pixmap buffer = 0;                         /**< Off-screen buffer. */
  GC gc = nullptr;                           /**< Graphics context. */
  int width = 0, height = 0;                 /**< Size of the buffer. */
  unsigned int depth = 0;                    /**< Color depth of the screen. */
  XFontStruct *fonts[UI_FONT_COUNT] = {};    /**< Loaded fonts, indexed by ui_font. */

  ~x11_state(){ release(); }

  /** @brief Frees every resource that exists, in reverse order of creation. */
  void release() noexcept{
    if(!dpy) return;
    for(XFontStruct *&f : fonts){
      if(f) XFreeFont(dpy, f);
      f = nullptr;
    }
    if(buffer) XFreePixmap(dpy, buffer);
    if(gc) XFreeGC(dpy, gc);
    if(win) XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    *this = x11_state();
  }

  /** @brief The font for a ui_font value. */
  XFontStruct* font(ui_font f) const noexcept{ return fonts[static_cast<int>(f)]; }

  x11_state() = default;
  x11_state& operator=(const x11_state &) = default;
};

x11_state x11;

/**
 * @brief  Loads the first available font from a list.
 * @param  names NULL-terminated list of font names, in order of preference.
 * @return The loaded font, or NULL if none is available.
 */
XFontStruct* load_font(const char *const *names){
  for(int i = 0; names[i]; i++){
    XFontStruct *f = XLoadQueryFont(x11.dpy, names[i]);
    if(f) return f;
  }
  return NULL;
}

/**
 * @brief  Translates an X11 key symbol into a ui_key value.
 * @param  sym Key symbol.
 * @return The key, or ui_key::none if the key is not used.
 */
ui_key translate_key(KeySym sym){
  switch(sym){
    case XK_Up:          return ui_key::up;
    case XK_Down:        return ui_key::down;
    case XK_Page_Up:     return ui_key::page_up;
    case XK_Page_Down:   return ui_key::page_down;
    case XK_Home:        return ui_key::home;
    case XK_End:         return ui_key::end;
    case XK_Tab:         return ui_key::tab;
    case XK_ISO_Left_Tab:return ui_key::back_tab;
    case XK_Return:
    case XK_KP_Enter:    return ui_key::enter;
    case XK_Escape:      return ui_key::escape;
    case XK_BackSpace:   return ui_key::backspace;
    case XK_Delete:      return ui_key::del;
  }
  return ui_key::none;
}

}

bool platform_open(int width, int height, const char *title, unsigned long background){
  x11.dpy = XOpenDisplay(NULL);
  if(!x11.dpy){
    platform_error("Could not open the X11 display.");
    return false;
  }

  static const char *const names[UI_FONT_COUNT][4] = {
    {"9x15", "fixed", NULL},
    {"9x15bold", "9x15", "fixed", NULL},
    {"10x20", "9x15bold", "fixed", NULL},
  };
  for(int i = 0; i < UI_FONT_COUNT; i++){
    x11.fonts[i] = load_font(names[i]);
    if(!x11.fonts[i]){
      platform_error("Could not load an X11 font.");
      x11.release();
      return false;
    }
  }

  int scr = XDefaultScreen(x11.dpy);
  x11.width = width;
  x11.height = height;
  x11.depth = static_cast<unsigned int>(XDefaultDepth(x11.dpy, scr));
  x11.win = XCreateSimpleWindow(x11.dpy, XRootWindow(x11.dpy, scr), 0, 0, static_cast<unsigned int>(width),
                                static_cast<unsigned int>(height), 0, 0, background);
  XStoreName(x11.dpy, x11.win, title);
  XSelectInput(x11.dpy, x11.win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);

  XSizeHints hints;
  memset(&hints, 0, sizeof(hints));
  hints.flags = PMinSize;
  hints.min_width = width;
  hints.min_height = height;
  XSetWMNormalHints(x11.dpy, x11.win, &hints);

  x11.wm_delete = XInternAtom(x11.dpy, "WM_DELETE_WINDOW", 0);
  XSetWMProtocols(x11.dpy, x11.win, &x11.wm_delete, 1);
  x11.buffer = XCreatePixmap(x11.dpy, x11.win, static_cast<unsigned int>(width), static_cast<unsigned int>(height),
                             x11.depth);
  x11.gc = XCreateGC(x11.dpy, x11.win, 0, NULL);
  XMapWindow(x11.dpy, x11.win);
  return true;
}

void platform_set_redraw(void (*redraw)()){
  /* X11 always returns events, so main() repaints after ui_event_type::resize. */
  (void) redraw;
}

int platform_width(){
  return x11.width;
}

int platform_height(){
  return x11.height;
}

void platform_close(){
  x11.release();
}

void platform_error(const char *message){
  fprintf(stderr, "%s\n", message);
}

void platform_next_event(ui_event *event){
  XEvent xe;
  XNextEvent(x11.dpy, &xe);
  *event = ui_event();

  /* Each case copies the member of the union that the event type selects. */
  if(xe.type == Expose){
    event->type = ui_event_type::expose;
  }else if(xe.type == ConfigureNotify){
    XConfigureEvent c;
    memcpy(&c, &xe, sizeof(c));
    /* Also sent when the window only moves. */
    if(c.width != x11.width || c.height != x11.height){
      XFreePixmap(x11.dpy, x11.buffer);
      x11.buffer = XCreatePixmap(x11.dpy, x11.win, static_cast<unsigned int>(c.width),
                                 static_cast<unsigned int>(c.height), x11.depth);
      x11.width = c.width;
      x11.height = c.height;
      event->type = ui_event_type::resize;
    }
  }else if(xe.type == ButtonPress){
    XButtonEvent b;
    memcpy(&b, &xe, sizeof(b));
    event->x = b.x;
    event->y = b.y;
    event->time = b.time;
    /* Buttons 4 and 5 correspond to the mouse wheel. */
    if(b.button == 1) event->type = ui_event_type::click;
    else if(b.button == 4 || b.button == 5){
      event->type = ui_event_type::wheel;
      event->wheel = b.button == 4 ? -1 : 1;
    }
  }else if(xe.type == KeyPress){
    XKeyEvent k;
    memcpy(&k, &xe, sizeof(k));
    char text[8];
    KeySym sym;
    int n = XLookupString(&k, text, sizeof(text), &sym, NULL);
    event->key = translate_key(sym);
    if(event->key == ui_key::none && n == 1 && text[0] >= 32 && text[0] < 127){
      event->key = ui_key::character;
      event->ch = text[0];
    }
    if(event->key != ui_key::none) event->type = ui_event_type::key;
  }else if(xe.type == ClientMessage){
    XClientMessageEvent m;
    memcpy(&m, &xe, sizeof(m));
    if(static_cast<Atom>(m.data.l[0]) == x11.wm_delete) event->type = ui_event_type::quit;
  }
}

void platform_fill_rect(int x, int y, int w, int h, unsigned long color){
  XSetForeground(x11.dpy, x11.gc, color);
  XFillRectangle(x11.dpy, x11.buffer, x11.gc, x, y, static_cast<unsigned int>(w), static_cast<unsigned int>(h));
}

void platform_draw_rect(int x, int y, int w, int h, unsigned long color){
  XSetForeground(x11.dpy, x11.gc, color);
  XDrawRectangle(x11.dpy, x11.buffer, x11.gc, x, y, static_cast<unsigned int>(w) - 1, static_cast<unsigned int>(h) - 1);
}

void platform_draw_text(ui_font font, int x, int baseline, const char *s, int len, unsigned long color){
  XSetForeground(x11.dpy, x11.gc, color);
  XSetFont(x11.dpy, x11.gc, x11.font(font)->fid);
  XDrawString(x11.dpy, x11.buffer, x11.gc, x, baseline, s, len);
}

int platform_text_width(ui_font font, const char *s, int len){
  return XTextWidth(x11.font(font), s, len);
}

int platform_font_ascent(ui_font font){
  return x11.font(font)->ascent;
}

int platform_font_descent(ui_font font){
  return x11.font(font)->descent;
}

void platform_present(){
  XCopyArea(x11.dpy, x11.buffer, x11.win, x11.gc, 0, 0, static_cast<unsigned int>(x11.width),
            static_cast<unsigned int>(x11.height), 0, 0);
  XFlush(x11.dpy);
}

}
