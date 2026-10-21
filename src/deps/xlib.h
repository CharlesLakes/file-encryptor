/**
 * @file    xlib.h
 * @brief   Minimal hand-written Xlib declarations.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section xlib_overview Overview
 *
 * These declarations allow the program to link against libX11.so.6 without
 * the libx11-dev headers. Only the types, constants and functions required by
 * the user interface are declared. Structure layouts match the official
 * <X11/Xlib.h> on 64-bit Linux.
 */

#ifndef DEPS_XLIB_H
#define DEPS_XLIB_H

typedef struct _XDisplay Display;
typedef struct _XGC *GC;
typedef unsigned long XID;
typedef XID Window;
typedef XID Drawable;
typedef XID Pixmap;
typedef XID Font;
typedef XID KeySym;
typedef unsigned long Atom;
typedef unsigned long Time;
typedef int Bool;

typedef struct{
  short lbearing, rbearing, width, ascent, descent;
  unsigned short attributes;
}XCharStruct;

typedef struct{
  void *ext_data;
  Font fid;
  unsigned direction;
  unsigned min_char_or_byte2, max_char_or_byte2;
  unsigned min_byte1, max_byte1;
  Bool all_chars_exist;
  unsigned default_char;
  int n_properties;
  void *properties;
  XCharStruct min_bounds, max_bounds;
  XCharStruct *per_char;
  int ascent, descent;
}XFontStruct;

typedef struct{
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window, root, subwindow;
  Time time;
  int x, y, x_root, y_root;
  unsigned int state;
  unsigned int button;
  Bool same_screen;
}XButtonEvent;

typedef struct{
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window, root, subwindow;
  Time time;
  int x, y, x_root, y_root;
  unsigned int state;
  unsigned int keycode;
  Bool same_screen;
}XKeyEvent;

typedef struct{
  int type;
  unsigned long serial;
  Bool send_event;
  Display *display;
  Window window;
  Atom message_type;
  int format;
  union{ char b[20]; short s[10]; long l[5]; }data;
}XClientMessageEvent;

typedef union{
  int type;
  XButtonEvent xbutton;
  XKeyEvent xkey;
  XClientMessageEvent xclient;
  long pad[24];
}XEvent;

#define KeyPress          2
#define ButtonPress       4
#define Expose            12
#define ClientMessage     33

#define KeyPressMask      (1L << 0)
#define ButtonPressMask   (1L << 2)
#define ExposureMask      (1L << 15)

#define XK_BackSpace      0xff08
#define XK_Tab            0xff09
#define XK_Return         0xff0d
#define XK_Escape         0xff1b
#define XK_Home           0xff50
#define XK_Up             0xff52
#define XK_Down           0xff54
#define XK_Page_Up        0xff55
#define XK_Page_Down      0xff56
#define XK_End            0xff57
#define XK_KP_Enter       0xff8d
#define XK_ISO_Left_Tab   0xfe20
#define XK_Delete         0xffff

Display*     XOpenDisplay(const char *name);
int          XCloseDisplay(Display *dpy);
int          XDefaultScreen(Display *dpy);
int          XDefaultDepth(Display *dpy, int screen);
Window       XRootWindow(Display *dpy, int screen);
Window       XCreateSimpleWindow(Display *dpy, Window parent, int x, int y, unsigned int w, unsigned int h,
                                 unsigned int border_width, unsigned long border, unsigned long background);
int          XDestroyWindow(Display *dpy, Window w);
int          XStoreName(Display *dpy, Window w, const char *name);
int          XSelectInput(Display *dpy, Window w, long mask);
int          XMapWindow(Display *dpy, Window w);
Atom         XInternAtom(Display *dpy, const char *name, Bool only_if_exists);
int          XSetWMProtocols(Display *dpy, Window w, Atom *protocols, int count);
int          XNextEvent(Display *dpy, XEvent *event);
GC           XCreateGC(Display *dpy, Drawable d, unsigned long mask, void *values);
int          XFreeGC(Display *dpy, GC gc);
int          XSetForeground(Display *dpy, GC gc, unsigned long color);
int          XSetFont(Display *dpy, GC gc, Font font);
int          XFillRectangle(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h);
int          XDrawRectangle(Display *dpy, Drawable d, GC gc, int x, int y, unsigned int w, unsigned int h);
int          XDrawString(Display *dpy, Drawable d, GC gc, int x, int y, const char *s, int len);
Pixmap       XCreatePixmap(Display *dpy, Drawable d, unsigned int w, unsigned int h, unsigned int depth);
int          XFreePixmap(Display *dpy, Pixmap p);
int          XCopyArea(Display *dpy, Drawable src, Drawable dst, GC gc, int sx, int sy,
                       unsigned int w, unsigned int h, int dx, int dy);
XFontStruct* XLoadQueryFont(Display *dpy, const char *name);
int          XFreeFont(Display *dpy, XFontStruct *font);
int          XTextWidth(XFontStruct *font, const char *s, int len);
int          XLookupString(XKeyEvent *event, char *buffer, int size, KeySym *keysym, void *status);
int          XFlush(Display *dpy);

#endif
