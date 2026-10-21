/**
 * @file    layout.cpp
 * @brief   Application state, list geometry and the buttons of each screen.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.h"

#include <string.h>

app_state app;

void app_init(void){
  memset(&app, 0, sizeof(app));
  app.screen = SCREEN_OPEN;
  app.entry_sel = -1;
  app.confirm_delete = -1;
  app.focus = FIELD_PATH;
  app.last_click_index = -1;
  archive_init(&app.arc);
  browser_init(&app.files);
}

void app_free(void){
  archive_free(&app.arc);
  browser_free(&app.files);
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
  *y = app.screen == SCREEN_ARCHIVE ? 92 : 72;
  int bottom = (app.screen == SCREEN_OPEN ? buttons_y() - FIELDS_ABOVE : buttons_y()) - 16;
  /* Whole rows only. */
  *h = (bottom - *y) / ROW_H * ROW_H;
  if(*h < ROW_H) *h = ROW_H;
}

int list_count(void){
  return (int) (app.screen == SCREEN_ARCHIVE ? app.arc.entries.n : app.files.items.n);
}

int* list_sel(void){
  return app.screen == SCREEN_ARCHIVE ? &app.entry_sel : &app.files.sel;
}

int* list_scroll(void){
  return app.screen == SCREEN_ARCHIVE ? &app.entry_scroll : &app.files.scroll;
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

void field_rect(int index, int *x, int *y, int *w, int *h){
  *x = FIELD_X;
  *y = buttons_y() - FIELDS_ABOVE + index*FIELD_STEP;
  *w = platform_width() - MARGIN - FIELD_X;
  *h = FIELD_H;
}

int get_buttons(button *b){
  /* One row per screen, in SCREEN_* order. */
  static const struct{ const char *label; int action; unsigned long color; } layouts[4][MAX_BUTTONS] = {
    {{"Open", ACT_OPEN, COLOR_PRIMARY}, {"Create new", ACT_CREATE, COLOR_BUTTON}},
    {{"Add", ACT_ADD, COLOR_PRIMARY}, {"Extract", ACT_EXTRACT, COLOR_BUTTON},
     {"Extract all", ACT_EXTRACT_ALL, COLOR_BUTTON}, {"Delete", ACT_DELETE, COLOR_DANGER},
     {"Compact", ACT_COMPACT, COLOR_BUTTON}, {"Close", ACT_CLOSE, COLOR_BUTTON}},
    {{"Add", ACT_PICK_CONFIRM, COLOR_PRIMARY}, {"Cancel", ACT_PICK_CANCEL, COLOR_BUTTON}},
    {{"Extract here", ACT_PICK_CONFIRM, COLOR_PRIMARY}, {"Cancel", ACT_PICK_CANCEL, COLOR_BUTTON}},
  };

  int count = 0, x = MARGIN;
  for(int i = 0; i < MAX_BUTTONS && layouts[app.screen][i].label; i++){
    const char *label = layouts[app.screen][i].label;
    int action = layouts[app.screen][i].action;
    /* A remote archive only grows by uploads: deleting and compacting happen on the server. */
    if(app.screen == SCREEN_ARCHIVE && app.arc.is_remote && (action == ACT_DELETE || action == ACT_COMPACT)) continue;
    b[count].label = label;
    b[count].action = layouts[app.screen][i].action;
    b[count].color = layouts[app.screen][i].color;
    b[count].x = x;
    b[count].y = buttons_y();
    b[count].w = platform_text_width(FONT_BOLD, label, (int) strlen(label)) + 36;
    b[count].h = BUTTON_H;
    x += b[count].w + 10;
    count++;
  }
  return count;
}
