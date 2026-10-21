/**
 * @file    layout.c
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
  archive_init(&app.archive);
  browser_init(&app.browser);
}

void app_free(void){
  archive_free(&app.archive);
  browser_free(&app.browser);
  clear_secrets();
}

/* ---------------- List ---------------- */

void list_rect(int *x, int *y, int *w, int *h){
  *x = MARGIN;
  *w = WIDTH - 2*MARGIN;
  if(app.screen == SCREEN_OPEN){ *y = 72; *h = ROW_H*12; }
  else if(app.screen == SCREEN_ARCHIVE){ *y = 92; *h = ROW_H*16; }
  else{ *y = 72; *h = ROW_H*17; }
}

int list_count(void){
  return (int) (app.screen == SCREEN_ARCHIVE ? app.archive.entries.n : app.browser.items.n);
}

int* list_sel(void){
  return app.screen == SCREEN_ARCHIVE ? &app.entry_sel : &app.browser.sel;
}

int* list_scroll(void){
  return app.screen == SCREEN_ARCHIVE ? &app.entry_scroll : &app.browser.scroll;
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
  *y = 352 + index*36;
  *w = WIDTH - MARGIN - FIELD_X;
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
    b[count].label = label;
    b[count].action = layouts[app.screen][i].action;
    b[count].color = layouts[app.screen][i].color;
    b[count].x = x;
    b[count].y = BUTTONS_Y;
    b[count].w = XTextWidth(gui.bold_font, label, (int) strlen(label)) + 36;
    b[count].h = BUTTON_H;
    x += b[count].w + 10;
    count++;
  }
  return count;
}
