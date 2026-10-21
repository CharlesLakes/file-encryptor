/**
 * @file    ui.h
 * @brief   Graphical user interface: shared state, constants and functions.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section ui_screens Screens
 *
 * The window displays one of four screens:
 *
 * | Screen                  | Purpose                                              |
 * |-------------------------|------------------------------------------------------|
 * | ui_screen::open         | Select a .crls file, enter the password, open/create |
 * | ui_screen::archive      | Contents of the open archive and its actions         |
 * | ui_screen::pick_add     | Folder browser to select the file to add             |
 * | ui_screen::pick_extract | Folder browser to select the extraction destination  |
 *
 * @section ui_flow Event Flow
 *
 * main.cpp receives each event from the platform layer (platform.h), input.cpp
 * translates it into an action, actions.cpp executes it (delegating to
 * core/archive.cpp) and draw.cpp repaints the window.
 *
 * @section ui_files Source Files
 *
 * | File             | Responsibility                                     |
 * |------------------|----------------------------------------------------|
 * | layout.cpp       | List geometry, text fields and buttons             |
 * | draw.cpp         | Painting of each screen                            |
 * | actions.cpp      | Behavior of each button and key                    |
 * | input.cpp        | Mouse and keyboard handling                        |
 * | platform_*.cpp   | Window, events and drawing for each window system  |
 *
 * The state is one app_state object owned by main() and passed by reference
 * to every function that needs it; there are no UI globals.
 */

#ifndef UI_UI_H
#define UI_UI_H

#include <array>
#include <span>
#include <string>
#include <string_view>
#include "core/archive.h"
#include "core/browser.h"
#include "lib/secret.h"
#include "ui/platform.h"

namespace crls{

/* ---------------- Geometry (pixels) ---------------- */

/*
 * The window can be resized. Titles stay at the top, while the buttons, the
 * status line and the text fields are anchored to the bottom edge; lists grow
 * to fill the space in between.
 */

inline constexpr int MIN_WIDTH      = 760;   /**< Initial and minimum client width. */
inline constexpr int MIN_HEIGHT     = 560;   /**< Initial and minimum client height. */
inline constexpr int MARGIN         = 16;
inline constexpr int ROW_H          = 22;
inline constexpr int BUTTONS_BOTTOM = 90;   /**< Distance from the button row to the bottom edge. */
inline constexpr int BUTTON_H       = 34;
inline constexpr int STATUS_BOTTOM  = 24;   /**< Distance from the status baseline to the bottom edge. */
inline constexpr int FIELDS_ABOVE   = 118;   /**< Distance from the first field to the button row. */
inline constexpr int FIELD_STEP     = 36;   /**< Vertical distance between fields. */
inline constexpr int FIELD_X        = 190;
inline constexpr int FIELD_H        = 28;
inline constexpr std::size_t MAX_BUTTONS = 6;
inline constexpr std::size_t MAX_PATH_FIELD = 4096;   /**< Longest text accepted in the path field. */

/* ---------------- Colors (0xRRGGBB) ---------------- */

inline constexpr unsigned long COLOR_BG       = 0x1e1e2e;
inline constexpr unsigned long COLOR_PANEL    = 0x181825;
inline constexpr unsigned long COLOR_ROW_ALT  = 0x1b1b2b;
inline constexpr unsigned long COLOR_SELECTED = 0x45475a;
inline constexpr unsigned long COLOR_TEXT     = 0xcdd6f4;
inline constexpr unsigned long COLOR_MUTED    = 0x7f849c;
inline constexpr unsigned long COLOR_DIR      = 0x89b4fa;
inline constexpr unsigned long COLOR_FILE     = 0xa6e3a1;
inline constexpr unsigned long COLOR_ERROR    = 0xf38ba8;
inline constexpr unsigned long COLOR_OK       = 0xa6e3a1;
inline constexpr unsigned long COLOR_BUTTON   = 0x313244;
inline constexpr unsigned long COLOR_PRIMARY  = 0x89b4fa;
inline constexpr unsigned long COLOR_DANGER   = 0xf38ba8;
inline constexpr unsigned long COLOR_DARK_TEXT = 0x11111b;
inline constexpr unsigned long COLOR_BORDER   = 0x45475a;
inline constexpr unsigned long COLOR_FOCUS    = 0x89b4fa;

/* ---------------- State ---------------- */

/** @brief Screens of the application (see the table above). */
enum class ui_screen{ open, archive, pick_add, pick_extract };

/** @brief Actions triggered by buttons and keys. */
enum class ui_action{
  none, open, create, add, extract, extract_all, remove, compact, close, pick_confirm, pick_cancel
};

/** @brief Text fields of ui_screen::open, in focus order. */
enum class ui_field{ path, password, confirm };

/** @brief Every ui_field, in focus order. */
inline constexpr std::array<ui_field, 3> UI_FIELDS = {ui_field::path, ui_field::password, ui_field::confirm};

/**
 * @brief A rectangle in window coordinates.
 */
struct rect{
  int x = 0, y = 0;              /**< Top left corner. */
  int w = 0, h = 0;              /**< Size. */

  /**
   * @brief  Checks whether a point lies inside the rectangle.
   * @param  px Point x coordinate.
   * @param  py Point y coordinate.
   * @return True if the point is inside.
   */
  constexpr bool contains(int px, int py) const noexcept{
    return px >= x && px < x + w && py >= y && py < y + h;
  }
};

/**
 * @brief Button drawn on the current screen.
 */
struct button{
  rect area;                     /**< Bounding rectangle. */
  std::string_view label;        /**< Displayed text (a string literal). */
  ui_action action = ui_action::none; /**< Action triggered on click. */
  unsigned long color = 0;       /**< Background color. */
};

/**
 * @brief The buttons of the current screen, left to right.
 */
struct button_row{
  std::array<button, MAX_BUTTONS> slots{}; /**< Storage; only the first count are used. */
  std::size_t count = 0;                   /**< Number of buttons. */

  /** @brief The buttons. */
  std::span<const button> list() const noexcept{ return std::span(slots).first(count); }
};

/**
 * @brief Application state shared by all UI modules.
 */
struct app_state{
  /** @brief Starts on ui_screen::open, browsing the current folder. */
  app_state(){ files.open_cwd(); }

  ui_screen screen = ui_screen::open;    /**< Current screen. */
  archive arc;                           /**< Currently open archive, if any (closed on destruction). */
  browser files;                         /**< Folder browser used by the browsing screens. */

  bool extract_all = false;              /**< ui_screen::pick_extract: all files or only the selected one. */
  int entry_sel = -1;                    /**< Selection in ui_screen::archive (-1 = none). */
  int entry_scroll = 0;                  /**< Scroll position in ui_screen::archive. */
  int confirm_delete = -1;               /**< Entry awaiting a second Delete press, or -1. */

  std::string path_field;                /**< Content of the path field. */
  secret_string<MAX_PASSWORD> password_field; /**< Content of the password field (wiped on destruction). */
  secret_string<MAX_PASSWORD> confirm_field;  /**< Content of the confirmation field (wiped on destruction). */
  ui_field focus = ui_field::path;       /**< Field with keyboard focus. */

  unsigned long last_click_time = 0;     /**< Time of the last click, for double-click detection. */
  int last_click_index = -1;             /**< Row of the last click, for double-click detection. */
};


/* ---------------- layout.cpp ---------------- */

/**
 * @brief  Returns the top coordinate of the button row.
 * @return Y coordinate in pixels.
 */
int  buttons_y();

/**
 * @brief  Returns the baseline of the status line.
 * @return Y coordinate in pixels.
 */
int  status_y();

/**
 * @brief  Computes the rectangle of the list on the current screen, which
 *         fills the space between the header and the fields or buttons.
 * @param  app Application state.
 * @return The rectangle.
 */
rect list_rect(const app_state &app);

/**
 * @brief  Returns the number of rows of the current list (archive entries or
 *         browser items).
 * @param app Application state.
 * @return Number of rows.
 */
int  list_count(const app_state &app);

/**
 * @brief  Returns the selected row of the current list.
 * @param  app Application state.
 * @return The selection index (-1 = none), which may be changed.
 */
int& list_sel(app_state &app);

/**
 * @brief  Returns the scroll position of the current list.
 * @param  app Application state.
 * @return The index of the first visible row, which may be changed.
 */
int& list_scroll(app_state &app);

/**
 * @brief  Returns the number of rows that fit in the current list.
 * @param app Application state.
 * @return Number of visible rows.
 */
int  visible_rows(const app_state &app);

/**
 * @brief Restricts the scroll position of the current list to its valid range.
 * @param app Application state.
 */
void clamp_scroll(app_state &app);

/**
 * @brief Adjusts the scroll position so that the selected row is visible.
 * @param app Application state.
 */
void ensure_visible(app_state &app);

/**
 * @brief  Computes the rectangle of a text field on ui_screen::open.
 * @param  field Field.
 * @return The rectangle.
 */
rect field_rect(ui_field field);

/**
 * @brief  Lays out the buttons of the current screen.
 * @param  app Application state.
 * @return The buttons.
 */
button_row get_buttons(const app_state &app);

/* ---------------- draw.cpp ---------------- */

/**
 * @brief Repaints the whole window.
 * @param app Application state.
 */
void draw(app_state &app);

/* ---------------- actions.cpp ---------------- */

/**
 * @brief Executes an action.
 * @param app Application state.
 * @param action Action.
 */
void handle_action(app_state &app, ui_action action);

/**
 * @brief Activates the selected row (Enter key or double click).
 * @param app Application state.
 */
void activate_selection(app_state &app);

/**
 * @brief Moves the selection of the current list, clamped to its bounds.
 * @param app Application state.
 * @param delta Number of rows to move (negative moves up).
 */
void move_selection(app_state &app, int delta);

/**
 * @brief Selects a row of the current list.
 * @param app Application state.
 * @param index Row to select.
 */
void select_row(app_state &app, int index);

/**
 * @brief Wipes the password and confirmation fields.
 * @param app Application state.
 */
void clear_secrets(app_state &app);

/* ---------------- input.cpp ---------------- */

/**
 * @brief Handles a mouse click or wheel event.
 * @param app Application state.
 * @param event ui_event_type::click or ui_event_type::wheel event.
 */
void handle_click(app_state &app, const ui_event &event);

/**
 * @brief Handles a key press.
 * @param app Application state.
 * @param event ui_event_type::key event.
 */
void handle_key(app_state &app, const ui_event &event);

}

#endif
