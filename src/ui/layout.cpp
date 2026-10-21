/**
 * @file    layout.cpp
 * @brief   Application state, list geometry and the buttons of each screen.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.h"

#include <string.h>

namespace crls{

app_state app;

void app_init(void){
  app.arc.close();
  app.screen = ui_screen::open;
  app.extract_all = false;
  app.entry_sel = -1;
  app.entry_scroll = 0;
  app.confirm_delete = -1;
  app.path_field[0] = '\0';
  clear_secrets();
  app.focus = ui_field::path;
  app.last_click_time = 0;
  app.last_click_index = -1;
  app.files.open_cwd();
}

void app_free(void){
  app.arc.close();
  clear_secrets();
}

/* ---------------- Anchors ---------------- */

int buttons_y(void){
  return platform_height() - BUTTONS_BOTTOM;
}

int status_y(void){
  return platform_height() - STATUS_BOTTOM;
}

/* ---------------- List ---------------- */

void list_rect(int *x, int *y, int *w, int *h){
  *x = MARGIN;
  *w = platform_width() - 2*MARGIN;
  *y = app.screen == ui_screen::archive ? 92 : 72;
  int bottom = (app.screen == ui_screen::open ? buttons_y() - FIELDS_ABOVE : buttons_y()) - 16;
  /* Whole rows only. */
  *h = (bottom - *y) / ROW_H * ROW_H;
  if(*h < ROW_H) *h = ROW_H;
}

int list_count(void){
  return (int) (app.screen == ui_screen::archive ? app.arc.entries().size() : app.files.items().size());
}

int* list_sel(void){
  return app.screen == ui_screen::archive ? &app.entry_sel : &app.files.sel;
}

int* list_scroll(void){
  return app.screen == ui_screen::archive ? &app.entry_scroll : &app.files.scroll;
}

int visible_rows(void){
  int x, y, w, h;
  list_rect(&x, &y, &w, &h);
  return h / ROW_H;
}

void clamp_scroll(void){
  int *scroll = list_scroll();
  int max_scroll = list_count() - visible_rows();
  if(*scroll > max_scroll) *scroll = max_scroll;
  if(*scroll < 0) *scroll = 0;
}

void ensure_visible(void){
  int sel = *list_sel(), *scroll = list_scroll();
  if(sel < 0) return;
  if(sel < *scroll) *scroll = sel;
  if(sel >= *scroll + visible_rows()) *scroll = sel - visible_rows() + 1;
  clamp_scroll();
}

/* ---------------- Fields and buttons ---------------- */

void field_rect(ui_field field, int *x, int *y, int *w, int *h){
  *x = FIELD_X;
  *y = buttons_y() - FIELDS_ABOVE + static_cast<int>(field) * FIELD_STEP;
  *w = platform_width() - MARGIN - FIELD_X;
  *h = FIELD_H;
}

int get_buttons(button *b){
  /* One row per screen, in ui_screen order. */
  static const struct{ const char *label; ui_action action; unsigned long color; } layouts[4][MAX_BUTTONS] = {
    {{"Open", ui_action::open, COLOR_PRIMARY}, {"Create new", ui_action::create, COLOR_BUTTON}},
    {{"Add", ui_action::add, COLOR_PRIMARY}, {"Extract", ui_action::extract, COLOR_BUTTON},
     {"Extract all", ui_action::extract_all, COLOR_BUTTON}, {"Delete", ui_action::remove, COLOR_DANGER},
     {"Compact", ui_action::compact, COLOR_BUTTON}, {"Close", ui_action::close, COLOR_BUTTON}},
    {{"Add", ui_action::pick_confirm, COLOR_PRIMARY}, {"Cancel", ui_action::pick_cancel, COLOR_BUTTON}},
    {{"Extract here", ui_action::pick_confirm, COLOR_PRIMARY}, {"Cancel", ui_action::pick_cancel, COLOR_BUTTON}},
  };

  int count = 0, x = MARGIN;
  for(int i = 0; i < MAX_BUTTONS && layouts[static_cast<int>(app.screen)][i].label; i++){
    const char *label = layouts[static_cast<int>(app.screen)][i].label;
    ui_action action = layouts[static_cast<int>(app.screen)][i].action;
    /* A remote archive only grows by uploads: deleting and compacting happen on the server. */
    if(app.screen == ui_screen::archive && app.arc.is_remote() && (action == ui_action::remove || action == ui_action::compact)) continue;
    b[count].label = label;
    b[count].action = layouts[static_cast<int>(app.screen)][i].action;
    b[count].color = layouts[static_cast<int>(app.screen)][i].color;
    b[count].x = x;
    b[count].y = buttons_y();
    b[count].w = platform_text_width(FONT_BOLD, label, (int) strlen(label)) + 36;
    b[count].h = BUTTON_H;
    x += b[count].w + 10;
    count++;
  }
  return count;
}

}
