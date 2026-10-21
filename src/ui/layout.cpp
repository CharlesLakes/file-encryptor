/**
 * @file    layout.cpp
 * @brief   List geometry, text fields and the buttons of each screen.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.hpp"

#include <algorithm>

namespace crls{

/* ---------------- Anchors ---------------- */

int buttons_y(){
  return platform_height() - BUTTONS_BOTTOM;
}

int status_y(){
  return platform_height() - STATUS_BOTTOM;
}

/* ---------------- List ---------------- */

rect list_rect(const app_state &app){
  rect r;
  r.x = MARGIN;
  r.w = platform_width() - 2*MARGIN;
  r.y = app.screen == ui_screen::archive ? 92 : 72;
  int bottom = (app.screen == ui_screen::open ? buttons_y() - FIELDS_ABOVE : buttons_y()) - 16;
  /* Whole rows only, and at least one. */
  r.h = std::max((bottom - r.y) / ROW_H * ROW_H, ROW_H);
  return r;
}

int list_count(const app_state &app){
  return static_cast<int>(app.screen == ui_screen::archive ? app.arc.entries().size() : app.files.items().size());
}

int& list_sel(app_state &app){
  return app.screen == ui_screen::archive ? app.entry_sel : app.files.sel;
}

int& list_scroll(app_state &app){
  return app.screen == ui_screen::archive ? app.entry_scroll : app.files.scroll;
}

int visible_rows(const app_state &app){
  return list_rect(app).h / ROW_H;
}

void clamp_scroll(app_state &app){
  int &scroll = list_scroll(app);
  /* Not std::clamp: the upper bound is negative when every row fits. */
  scroll = std::max(0, std::min(scroll, list_count(app) - visible_rows(app)));
}

void ensure_visible(app_state &app){
  int sel = list_sel(app), &scroll = list_scroll(app);
  if(sel < 0) return;
  if(sel < scroll) scroll = sel;
  if(sel >= scroll + visible_rows(app)) scroll = sel - visible_rows(app) + 1;
  clamp_scroll(app);
}

/* ---------------- Fields and buttons ---------------- */

rect field_rect(ui_field field){
  return rect{FIELD_X, buttons_y() - FIELDS_ABOVE + static_cast<int>(field) * FIELD_STEP,
              platform_width() - MARGIN - FIELD_X, FIELD_H};
}

/** @brief A button of a screen, before it is laid out. */
struct button_spec{
  std::string_view label;    /**< Displayed text. */
  ui_action action;          /**< Action triggered on click. */
  unsigned long color;       /**< Background color. */
};

static constexpr std::array<button_spec, 2> OPEN_BUTTONS = {{
  {"Open", ui_action::open, COLOR_PRIMARY}, {"Create new", ui_action::create, COLOR_BUTTON},
}};
static constexpr std::array<button_spec, 6> ARCHIVE_BUTTONS = {{
  {"Add", ui_action::add, COLOR_PRIMARY}, {"Extract", ui_action::extract, COLOR_BUTTON},
  {"Extract all", ui_action::extract_all, COLOR_BUTTON}, {"Delete", ui_action::remove, COLOR_DANGER},
  {"Compact", ui_action::compact, COLOR_BUTTON}, {"Close", ui_action::close, COLOR_BUTTON},
}};
static constexpr std::array<button_spec, 2> PICK_ADD_BUTTONS = {{
  {"Add", ui_action::pick_confirm, COLOR_PRIMARY}, {"Cancel", ui_action::pick_cancel, COLOR_BUTTON},
}};
static constexpr std::array<button_spec, 2> PICK_EXTRACT_BUTTONS = {{
  {"Extract here", ui_action::pick_confirm, COLOR_PRIMARY}, {"Cancel", ui_action::pick_cancel, COLOR_BUTTON},
}};

/**
 * @brief  Returns the buttons of a screen.
 * @param  screen Screen.
 * @return Its buttons, left to right.
 */
static std::span<const button_spec> screen_buttons(ui_screen screen){
  switch(screen){
    case ui_screen::open:         return OPEN_BUTTONS;
    case ui_screen::archive:      return ARCHIVE_BUTTONS;
    case ui_screen::pick_add:     return PICK_ADD_BUTTONS;
    case ui_screen::pick_extract: return PICK_EXTRACT_BUTTONS;
  }
  return {};
}

button_row get_buttons(const app_state &app){
  static_assert(ARCHIVE_BUTTONS.size() <= MAX_BUTTONS, "a screen has more buttons than MAX_BUTTONS");
  button_row row;
  int x = MARGIN;
  for(const button_spec &spec : screen_buttons(app.screen)){
    /* A remote archive only grows by uploads: deleting and compacting happen on the server. */
    bool server_only = spec.action == ui_action::remove || spec.action == ui_action::compact;
    if(app.screen == ui_screen::archive && app.arc.is_remote() && server_only) continue;
    int w = platform_text_width(ui_font::bold, spec.label) + 36;
    row.slots[row.count++] = button{rect{x, buttons_y(), w, BUTTON_H}, spec.label, spec.action, spec.color};
    x += w + 10;
  }
  return row;
}

}
