/**
 * @file    platform.h
 * @brief   Windowing system abstraction: window, events, drawing and fonts.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section platform_overview Overview
 *
 * The rest of the user interface only draws and receives events through these
 * functions, so it is independent of the windowing system. The backend is
 * selected at build time:
 *
 * | File               | Backend                 |
 * |--------------------|-------------------------|
 * | platform_x11.c     | Xlib (Linux)            |
 * | platform_win32.c   | Win32 and GDI (Windows) |
 *
 * All drawing goes to an off-screen buffer of WIDTH x HEIGHT pixels, which
 * platform_present() copies to the window. Colors are 0xRRGGBB values.
 */

#ifndef UI_PLATFORM_H
#define UI_PLATFORM_H

#include <stdbool.h>

/** @brief Fonts available to the user interface. */
enum{ FONT_REGULAR, FONT_BOLD, FONT_TITLE, FONT_COUNT };

/** @brief Kinds of events delivered by platform_next_event(). */
enum{
  EVENT_NONE,                /**< Nothing relevant happened. */
  EVENT_EXPOSE,              /**< The window must be repainted. */
  EVENT_CLICK,               /**< Left mouse button pressed. */
  EVENT_WHEEL,               /**< Mouse wheel turned. */
  EVENT_KEY,                 /**< Key pressed. */
  EVENT_QUIT                 /**< The user asked to close the window. */
};

/** @brief Keys recognized by the user interface. */
enum{
  KEY_NONE, KEY_CHAR, KEY_UP, KEY_DOWN, KEY_PAGE_UP, KEY_PAGE_DOWN, KEY_HOME, KEY_END,
  KEY_TAB, KEY_BACK_TAB, KEY_ENTER, KEY_ESCAPE, KEY_BACKSPACE, KEY_DELETE
};

/**
 * @brief Input event in a windowing system independent form.
 */
typedef struct{
  int type;                  /**< EVENT_* value. */
  int x, y;                  /**< Pointer position (EVENT_CLICK, EVENT_WHEEL). */
  int wheel;                 /**< Wheel direction: -1 up, +1 down (EVENT_WHEEL). */
  unsigned long time;        /**< Time stamp in milliseconds (EVENT_CLICK). */
  int key;                   /**< KEY_* value (EVENT_KEY). */
  char ch;                   /**< Printable ASCII character (KEY_CHAR). */
}ui_event;

/**
 * @brief  Creates and shows the window with a client area of width x height
 *         pixels and loads the fonts.
 * @param  width      Client area width.
 * @param  height     Client area height.
 * @param  title      Window title.
 * @param  background Color shown before the first repaint.
 * @return True on success, false otherwise (an error has been reported).
 */
bool platform_open(int width, int height, const char *title, unsigned long background);

/**
 * @brief Releases every windowing system resource.
 */
void platform_close(void);

/**
 * @brief Reports a fatal error to the user (standard error or a message box).
 * @param message Error message.
 */
void platform_error(const char *message);

/**
 * @brief Waits for the next event.
 * @param event Output: the event received.
 */
void platform_next_event(ui_event *event);

/**
 * @brief Fills a rectangle with a solid color.
 * @param x     Left coordinate.
 * @param y     Top coordinate.
 * @param w     Width.
 * @param h     Height.
 * @param color Fill color.
 */
void platform_fill_rect(int x, int y, int w, int h, unsigned long color);

/**
 * @brief Draws the one-pixel outline of a rectangle, inside its w x h area.
 * @param x     Left coordinate.
 * @param y     Top coordinate.
 * @param w     Width.
 * @param h     Height.
 * @param color Line color.
 */
void platform_draw_rect(int x, int y, int w, int h, unsigned long color);

/**
 * @brief Draws len characters of s.
 * @param font     FONT_* value.
 * @param x        Left coordinate.
 * @param baseline Baseline coordinate.
 * @param s        String.
 * @param len      Number of characters.
 * @param color    Text color.
 */
void platform_draw_text(int font, int x, int baseline, const char *s, int len, unsigned long color);

/**
 * @brief  Measures the width of len characters of s.
 * @param  font FONT_* value.
 * @param  s    String.
 * @param  len  Number of characters.
 * @return Width in pixels.
 */
int  platform_text_width(int font, const char *s, int len);

/**
 * @brief  Returns the distance from the baseline to the top of the font.
 * @param  font FONT_* value.
 * @return Ascent in pixels.
 */
int  platform_font_ascent(int font);

/**
 * @brief  Returns the distance from the baseline to the bottom of the font.
 * @param  font FONT_* value.
 * @return Descent in pixels.
 */
int  platform_font_descent(int font);

/**
 * @brief Copies the off-screen buffer to the window.
 */
void platform_present(void);

#endif
