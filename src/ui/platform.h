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
 * | platform_x11.cpp   | Xlib (Linux)            |
 * | platform_win32.cpp | Win32 and GDI (Windows) |
 *
 * All drawing goes to an off-screen buffer as large as the client area of the
 * window, which platform_present() copies to the window. The buffer follows
 * the window when it is resized (ui_event_type::resize). Colors are 0xRRGGBB values.
 */

#ifndef UI_PLATFORM_H
#define UI_PLATFORM_H

#include <string_view>
#include "lib/function_ref.h"

namespace crls{

/** @brief Fonts available to the user interface. */
enum class ui_font{ regular, bold, title };

inline constexpr int UI_FONT_COUNT = 3;   /**< Number of ui_font values. */

/** @brief Kinds of events delivered by platform_next_event(). */
enum class ui_event_type{
  none,                      /**< Nothing relevant happened. */
  expose,                    /**< The window must be repainted. */
  click,                     /**< Left mouse button pressed. */
  wheel,                     /**< Mouse wheel turned. */
  key,                       /**< Key pressed. */
  resize,                    /**< The client area changed size; see platform_width(). */
  quit                       /**< The user asked to close the window. */
};

/** @brief Keys recognized by the user interface. */
enum class ui_key{
  none, character, up, down, page_up, page_down, home, end,
  tab, back_tab, enter, escape, backspace, del
};

/**
 * @brief Input event in a windowing system independent form.
 */
struct ui_event{
  ui_event_type type = ui_event_type::none; /**< Kind of event. */
  int x = 0, y = 0;                         /**< Pointer position (click, wheel). */
  int wheel = 0;                            /**< Wheel direction: -1 up, +1 down (wheel). */
  unsigned long time = 0;                   /**< Time stamp in milliseconds (click). */
  ui_key key = ui_key::none;                /**< Key (key). */
  char ch = 0;                              /**< Printable ASCII character (ui_key::character). */
};

/**
 * @brief  Creates and shows a resizable window with a client area of
 *         width x height pixels, which is also its minimum size, and loads the
 *         fonts.
 * @param  width      Initial and minimum client area width.
 * @param  height     Initial and minimum client area height.
 * @param  title      Window title.
 * @param  background Color shown before the first repaint.
 * @return True on success, false otherwise (an error has been reported).
 */
bool platform_open(int width, int height, const char *title, unsigned long background);

/**
 * @brief Registers the function that repaints the window.
 *
 * Some backends cannot return events while the user is resizing the window
 * (Win32 runs its own message loop then); they call redraw directly so that
 * the contents follow the new size.
 *
 * @param redraw Function that repaints the whole window; it must stay alive
 *               until platform_set_redraw({}) is called or the window closes.
 */
void platform_set_redraw(function_ref<void()> redraw);

/**
 * @brief  Returns the current width of the client area and the buffer.
 * @return Width in pixels.
 */
int  platform_width();

/**
 * @brief  Returns the current height of the client area and the buffer.
 * @return Height in pixels.
 */
int  platform_height();

/**
 * @brief Releases every windowing system resource.
 */
void platform_close();

/**
 * @brief Reports a fatal error to the user (standard error or a message box).
 * @param message Error message.
 */
void platform_error(const char *message);

/**
 * @brief  Waits for the next event.
 * @return The event received.
 */
ui_event platform_next_event();

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
 * @brief Draws a string.
 * @param font     Font.
 * @param x        Left coordinate.
 * @param baseline Baseline coordinate.
 * @param s        String (it need not be NUL-terminated).
 * @param color    Text color.
 */
void platform_draw_text(ui_font font, int x, int baseline, std::string_view s, unsigned long color);

/**
 * @brief  Measures the width of a string.
 * @param  font Font.
 * @param  s    String (it need not be NUL-terminated).
 * @return Width in pixels.
 */
int  platform_text_width(ui_font font, std::string_view s);

/**
 * @brief  Returns the distance from the baseline to the top of the font.
 * @param  font Font.
 * @return Ascent in pixels.
 */
int  platform_font_ascent(ui_font font);

/**
 * @brief  Returns the distance from the baseline to the bottom of the font.
 * @param  font Font.
 * @return Descent in pixels.
 */
int  platform_font_descent(ui_font font);

/**
 * @brief Copies the off-screen buffer to the window.
 */
void platform_present();

}

#endif
