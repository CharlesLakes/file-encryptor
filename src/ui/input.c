/**
 * @file    input.c
 * @brief   Translation of mouse and keyboard events into actions.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.h"

#include <string.h>

#define DOUBLE_CLICK_MS 400   /**< Maximum interval between the clicks of a double click. */

/**
 * @brief  Checks whether a point lies inside a rectangle.
 * @param  px Point x coordinate.
 * @param  py Point y coordinate.
 * @param  x  Rectangle left coordinate.
 * @param  y  Rectangle top coordinate.
 * @param  w  Rectangle width.
 * @param  h  Rectangle height.
 * @return True if the point is inside, false otherwise.
 */
static bool inside(int px, int py, int x, int y, int w, int h){
  return px >= x && px < x + w && py >= y && py < y + h;
}

void handle_click(XButtonEvent *event){
  int px = event->x, py = event->y;
  int lx, ly, lw, lh;
  list_rect(&lx, &ly, &lw, &lh);

  /* Buttons 4 and 5 correspond to the mouse wheel. */
  if(event->button == 4 || event->button == 5){
    if(inside(px, py, lx, ly, lw, lh)){
      *list_scroll() += event->button == 4 ? -3 : 3;
      clamp_scroll();
    }
    return;
  }
  if(event->button != 1) return;

  button b[MAX_BUTTONS];
  int count = get_buttons(b);
  for(int i = 0; i < count; i++)
    if(inside(px, py, b[i].x, b[i].y, b[i].w, b[i].h)){
      handle_action(b[i].action);
      return;
    }

  if(app.screen == SCREEN_OPEN)
    for(int i = 0; i < FIELD_COUNT; i++){
      int fx, fy, fw, fh;
      field_rect(i, &fx, &fy, &fw, &fh);
      if(inside(px, py, fx, fy, fw, fh)){
        app.focus = i;
        return;
      }
    }

  if(inside(px, py, lx, ly, lw, lh)){
    int index = *list_scroll() + (py - ly) / ROW_H;
    if(index >= list_count()) return;

    bool double_click = index == app.last_click_index && event->time - app.last_click_time < DOUBLE_CLICK_MS;
    app.last_click_index = index;
    app.last_click_time = event->time;

    select_row(index);
    if(double_click){
      app.last_click_index = -1;
      activate_selection();
    }
  }
}

/**
 * @brief Appends a character to the focused field, or deletes its last character.
 * @param c     Character to append.
 * @param erase True to delete the last character instead.
 */
static void edit_field(char c, bool erase){
  char *field = app.focus == FIELD_PATH ? app.path_field
              : app.focus == FIELD_PASSWORD ? app.password_field : app.confirm_field;
  size_t max = app.focus == FIELD_PATH ? sizeof(app.path_field) - 1 : MAX_PASSWORD;
  size_t len = strlen(field);
  if(erase){
    if(len) field[len-1] = '\0';
  }else if(len < max){
    field[len] = c;
    field[len+1] = '\0';
  }
}

void handle_key(XKeyEvent *event){
  char text[8];
  KeySym sym;
  int n = XLookupString(event, text, sizeof(text), &sym, NULL);

  switch(sym){
    case XK_Up:        move_selection(-1); return;
    case XK_Down:      move_selection(1); return;
    case XK_Page_Up:   move_selection(-visible_rows()); return;
    case XK_Page_Down: move_selection(visible_rows()); return;
    case XK_Home:      move_selection(-list_count()); return;
    case XK_End:       move_selection(list_count()); return;
  }

  if(app.screen == SCREEN_OPEN){
    if(sym == XK_Tab){ app.focus = (app.focus + 1) % FIELD_COUNT; return; }
    if(sym == XK_ISO_Left_Tab){ app.focus = (app.focus + FIELD_COUNT - 1) % FIELD_COUNT; return; }
    if(sym == XK_Return || sym == XK_KP_Enter){
      handle_action(app.focus == FIELD_CONFIRM ? ACT_CREATE : ACT_OPEN);
      return;
    }
    if(sym == XK_BackSpace){ edit_field(0, true); return; }
    if(n == 1 && text[0] >= 32 && text[0] < 127) edit_field(text[0], false);
    return;
  }

  if(sym == XK_Return || sym == XK_KP_Enter){
    activate_selection();
  }else if(sym == XK_Escape && app.screen != SCREEN_ARCHIVE){
    handle_action(ACT_PICK_CANCEL);
  }else if(sym == XK_BackSpace && app.screen != SCREEN_ARCHIVE && strcmp(app.browser.dir, "/") != 0){
    browser_enter(&app.browser, "..");
  }else if(sym == XK_Delete && app.screen == SCREEN_ARCHIVE){
    handle_action(ACT_DELETE);
  }
}
