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
 * | Screen              | Purpose                                              |
 * |---------------------|------------------------------------------------------|
 * | SCREEN_OPEN         | Select a .crls file, enter the password, open/create |
 * | SCREEN_ARCHIVE      | Contents of the open archive and its actions         |
 * | SCREEN_PICK_ADD     | Folder browser to select the file to add             |
 * | SCREEN_PICK_EXTRACT | Folder browser to select the extraction destination  |
 *
 * @section ui_flow Event Flow
 *
 * main.c receives each event from the platform layer (platform.h), input.c
 * translates it into an action, actions.c executes it (delegating to
 * core/archive.c) and draw.c repaints the window.
 *
 * @section ui_files Source Files
 *
 * | File      | Responsibility                                          |
 * |-----------|---------------------------------------------------------|
 * | layout.c  | Application state (app), list geometry and buttons      |
 * | draw.c    | Painting of each screen                                 |
 * | actions.c | Behavior of each button and key                         |
 * | input.c   | Mouse and keyboard handling                             |
 * | platform_*.c | Window, events and drawing for each windowing system |
 */

#ifndef UI_UI_H
#define UI_UI_H

#include <stdbool.h>
#include <limits.h>
#include "core/archive.h"
#include "core/browser.h"
#include "ui/platform.h"

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

/** @brief Screens of the application. */
enum{ SCREEN_OPEN, SCREEN_ARCHIVE, SCREEN_PICK_ADD, SCREEN_PICK_EXTRACT };

/** @brief Actions triggered by buttons and keys. */
enum{
  ACT_NONE, ACT_OPEN, ACT_CREATE, ACT_ADD, ACT_EXTRACT, ACT_EXTRACT_ALL,
  ACT_DELETE, ACT_COMPACT, ACT_CLOSE, ACT_PICK_CONFIRM, ACT_PICK_CANCEL
};

/** @brief Text fields of SCREEN_OPEN, in focus order. */
enum{ FIELD_PATH, FIELD_PASSWORD, FIELD_CONFIRM, FIELD_COUNT };

/**
 * @brief Button drawn on the current screen.
 */
typedef struct{
  int x, y, w, h;                /**< Bounding rectangle. */
  const char *label;             /**< Displayed text. */
  int action;                    /**< ACT_* value triggered on click. */
  unsigned long color;           /**< Background color. */
}button;

/**
 * @brief Application state shared by all UI modules.
 */
typedef struct{
  int screen;                    /**< Current SCREEN_* value. */
  archive arc;                   /**< Currently open archive, if any. */
  browser files;                 /**< Folder browser used by the browsing screens. */

  bool extract_all;              /**< SCREEN_PICK_EXTRACT: all files or only the selected one. */
  int entry_sel, entry_scroll;   /**< Selection and scroll in SCREEN_ARCHIVE (-1 = none). */
  int confirm_delete;            /**< Entry awaiting a second Delete press, or -1. */

  char path_field[PATH_MAX];     /**< Content of the path field. */
  char password_field[MAX_PASSWORD + 1]; /**< Content of the password field. */
  char confirm_field[MAX_PASSWORD + 1];  /**< Content of the confirmation field. */
  int focus;                     /**< FIELD_* value with keyboard focus. */

  unsigned long last_click_time; /**< Time of the last click, for double-click detection. */
  int last_click_index;          /**< Row of the last click, for double-click detection. */
}app_state;

extern app_state app;            /**< Global application state (layout.c). */

/* ---------------- layout.c ---------------- */

/**
 * @brief Initializes the application state on SCREEN_OPEN.
 */
void app_init(void);

/**
 * @brief Releases the application state and wipes the password fields.
 */
void app_free(void);

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
 * @param x Output: left coordinate.
 * @param y Output: top coordinate.
 * @param w Output: width.
 * @param h Output: height.
 */
void list_rect(int *x, int *y, int *w, int *h);

/**
 * @brief  Returns the number of rows of the current list (archive entries or
 *         browser items).
 * @return Number of rows.
 */
int  list_count(void);

/**
 * @brief  Returns a pointer to the selected row of the current list.
 * @return Pointer to the selection index (-1 = none).
 */
int* list_sel(void);

/**
 * @brief  Returns a pointer to the scroll position of the current list.
 * @return Pointer to the index of the first visible row.
 */
int* list_scroll(void);

/**
 * @brief  Returns the number of rows that fit in the current list.
 * @return Number of visible rows.
 */
int  visible_rows(void);

/**
 * @brief Restricts the scroll position of the current list to its valid range.
 */
void clamp_scroll(void);

/**
 * @brief Adjusts the scroll position so that the selected row is visible.
 */
void ensure_visible(void);

/**
 * @brief Computes the rectangle of a text field on SCREEN_OPEN.
 * @param index FIELD_* value.
 * @param x     Output: left coordinate.
 * @param y     Output: top coordinate.
 * @param w     Output: width.
 * @param h     Output: height.
 */
void field_rect(int index, int *x, int *y, int *w, int *h);

/**
 * @brief  Fills b with the buttons of the current screen.
 * @param  b Destination array with room for at least MAX_BUTTONS elements.
 * @return Number of buttons written.
 */
int  get_buttons(button *b);

/* ---------------- draw.c ---------------- */

/**
 * @brief Repaints the whole window.
 */
void draw(void);

/* ---------------- actions.c ---------------- */

/**
 * @brief Executes an action.
 * @param action ACT_* value.
 */
void handle_action(int action);

/**
 * @brief Activates the selected row (Enter key or double click).
 */
void activate_selection(void);

/**
 * @brief Moves the selection of the current list, clamped to its bounds.
 * @param delta Number of rows to move (negative moves up).
 */
void move_selection(int delta);

/**
 * @brief Selects a row of the current list.
 * @param index Row to select.
 */
void select_row(int index);

/**
 * @brief Wipes the password and confirmation fields.
 */
void clear_secrets(void);

/* ---------------- input.c ---------------- */

/**
 * @brief Handles a mouse click or wheel event.
 * @param event EVENT_CLICK or EVENT_WHEEL event.
 */
void handle_click(const ui_event *event);

/**
 * @brief Handles a key press.
 * @param event EVENT_KEY event.
 */
void handle_key(const ui_event *event);

#endif
