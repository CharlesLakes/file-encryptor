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
 * | layout.cpp       | Initial state, list geometry and buttons           |
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

#include <stdbool.h>
#include <limits.h>
#include "core/archive.h"
#include "core/browser.h"
#include "ui/platform.h"

namespace crls{

/* ---------------- Geometry (pixels) ---------------- */

/*
 * The window can be resized. Titles stay at the top, while the buttons, the
 * status line and the text fields are anchored to the bottom edge; lists grow
 * to fill the space in between.
 */

#define MIN_WIDTH      760   /**< Initial and minimum client width. */
#define MIN_HEIGHT     560   /**< Initial and minimum client height. */
#define MARGIN         16
#define ROW_H          22
#define BUTTONS_BOTTOM 90    /**< Distance from the button row to the bottom edge. */
#define BUTTON_H       34
#define STATUS_BOTTOM  24    /**< Distance from the status baseline to the bottom edge. */
#define FIELDS_ABOVE   118   /**< Distance from the first field to the button row. */
#define FIELD_STEP     36    /**< Vertical distance between fields. */
#define FIELD_X        190
#define FIELD_H        28
#define MAX_BUTTONS    6

/* ---------------- Colors (0xRRGGBB) ---------------- */

#define COLOR_BG        0x1e1e2e
#define COLOR_PANEL     0x181825
#define COLOR_ROW_ALT   0x1b1b2b
#define COLOR_SELECTED  0x45475a
#define COLOR_TEXT      0xcdd6f4
#define COLOR_MUTED     0x7f849c
#define COLOR_DIR       0x89b4fa
#define COLOR_FILE      0xa6e3a1
#define COLOR_ERROR     0xf38ba8
#define COLOR_OK        0xa6e3a1
#define COLOR_BUTTON    0x313244
#define COLOR_PRIMARY   0x89b4fa
#define COLOR_DANGER    0xf38ba8
#define COLOR_DARK_TEXT 0x11111b
#define COLOR_BORDER    0x45475a
#define COLOR_FOCUS     0x89b4fa

/* ---------------- State ---------------- */

/** @brief Screens of the application (see the table above). */
enum class ui_screen{ open, archive, pick_add, pick_extract };

/** @brief Actions triggered by buttons and keys. */
enum class ui_action{
  none, open, create, add, extract, extract_all, remove, compact, close, pick_confirm, pick_cancel
};

/** @brief Text fields of ui_screen::open, in focus order. */
enum class ui_field{ path, password, confirm };

inline constexpr int UI_FIELD_COUNT = 3;   /**< Number of ui_field values. */

/**
 * @brief Button drawn on the current screen.
 */
typedef struct{
  int x, y, w, h;                /**< Bounding rectangle. */
  const char *label;             /**< Displayed text. */
  ui_action action;              /**< Action triggered on click. */
  unsigned long color;           /**< Background color. */
}button;

/**
 * @brief Application state shared by all UI modules.
 */
typedef struct{
  ui_screen screen;              /**< Current screen. */
  archive arc;                   /**< Currently open archive, if any. */
  browser files;                 /**< Folder browser used by the browsing screens. */

  bool extract_all;              /**< ui_screen::pick_extract: all files or only the selected one. */
  int entry_sel, entry_scroll;   /**< Selection and scroll in ui_screen::archive (-1 = none). */
  int confirm_delete;            /**< Entry awaiting a second Delete press, or -1. */

  char path_field[PATH_MAX];     /**< Content of the path field. */
  char password_field[MAX_PASSWORD + 1]; /**< Content of the password field. */
  char confirm_field[MAX_PASSWORD + 1];  /**< Content of the confirmation field. */
  ui_field focus;                /**< Field with keyboard focus. */

  unsigned long last_click_time; /**< Time of the last click, for double-click detection. */
  int last_click_index;          /**< Row of the last click, for double-click detection. */
}app_state;


/* ---------------- layout.cpp ---------------- */

/**
 * @brief Initializes the application state on ui_screen::open.
 * @param app Application state.
 */
void app_init(app_state &app);

/**
 * @brief Releases the application state and wipes the password fields.
 * @param app Application state.
 */
void app_free(app_state &app);

/**
 * @brief  Returns the top coordinate of the button row.
 * @return Y coordinate in pixels.
 */
int  buttons_y(void);

/**
 * @brief  Returns the baseline of the status line.
 * @return Y coordinate in pixels.
 */
int  status_y(void);

/**
 * @brief Computes the rectangle of the list on the current screen, which
 *        fills the space between the header and the fields or buttons.
 * @param app Application state.
 * @param x Output: left coordinate.
 * @param y Output: top coordinate.
 * @param w Output: width.
 * @param h Output: height.
 */
void list_rect(app_state &app, int *x, int *y, int *w, int *h);

/**
 * @brief  Returns the number of rows of the current list (archive entries or
 *         browser items).
 * @param app Application state.
 * @return Number of rows.
 */
int  list_count(app_state &app);

/**
 * @brief  Returns a pointer to the selected row of the current list.
 * @param app Application state.
 * @return Pointer to the selection index (-1 = none).
 */
int* list_sel(app_state &app);

/**
 * @brief  Returns a pointer to the scroll position of the current list.
 * @param app Application state.
 * @return Pointer to the index of the first visible row.
 */
int* list_scroll(app_state &app);

/**
 * @brief  Returns the number of rows that fit in the current list.
 * @param app Application state.
 * @return Number of visible rows.
 */
int  visible_rows(app_state &app);

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
 * @brief Computes the rectangle of a text field on ui_screen::open.
 * @param field Field.
 * @param x     Output: left coordinate.
 * @param y     Output: top coordinate.
 * @param w     Output: width.
 * @param h     Output: height.
 */
void field_rect(ui_field field, int *x, int *y, int *w, int *h);

/**
 * @brief  Fills b with the buttons of the current screen.
 * @param  app Application state.
 * @param  b Destination array with room for at least MAX_BUTTONS elements.
 * @return Number of buttons written.
 */
int  get_buttons(app_state &app, button *b);

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
void handle_click(app_state &app, const ui_event *event);

/**
 * @brief Handles a key press.
 * @param app Application state.
 * @param event ui_event_type::key event.
 */
void handle_key(app_state &app, const ui_event *event);

}

#endif
