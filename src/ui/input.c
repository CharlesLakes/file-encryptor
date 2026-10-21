/**
 * @file    input.c
 * @brief   Translation of mouse and keyboard events into actions.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.h"

#include <string.h>
#include "core/fs.h"

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

void handle_click(const ui_event *event){
  int px = event->x, py = event->y;
  int lx, ly, lw, lh;
  list_rect(&lx, &ly, &lw, &lh);

  if(event->type == EVENT_WHEEL){
    if(inside(px, py, lx, ly, lw, lh)){
      *list_scroll() += 3 * event->wheel;
      clamp_scroll();
    }
    return;
  }

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

void handle_key(const ui_event *event){
  int key = event->key;

  switch(key){
    case KEY_UP:        move_selection(-1); return;
    case KEY_DOWN:      move_selection(1); return;
    case KEY_PAGE_UP:   move_selection(-visible_rows()); return;
    case KEY_PAGE_DOWN: move_selection(visible_rows()); return;
    case KEY_HOME:      move_selection(-list_count()); return;
    case KEY_END:       move_selection(list_count()); return;
  }

  if(app.screen == SCREEN_OPEN){
    if(key == KEY_TAB){ app.focus = (app.focus + 1) % FIELD_COUNT; return; }
    if(key == KEY_BACK_TAB){ app.focus = (app.focus + FIELD_COUNT - 1) % FIELD_COUNT; return; }
    if(key == KEY_ENTER){
      handle_action(app.focus == FIELD_CONFIRM ? ACT_CREATE : ACT_OPEN);
      return;
    }
    if(key == KEY_BACKSPACE){ edit_field(0, true); return; }
    if(key == KEY_CHAR) edit_field(event->ch, false);
    return;
  }

  if(key == KEY_ENTER){
    activate_selection();
  }else if(key == KEY_ESCAPE && app.screen != SCREEN_ARCHIVE){
    handle_action(ACT_PICK_CANCEL);
  }else if(key == KEY_BACKSPACE && app.screen != SCREEN_ARCHIVE && !fs_is_root(app.files.dir)){
    browser_enter(&app.files, "..");
  }else if(key == KEY_DELETE && app.screen == SCREEN_ARCHIVE){
    handle_action(ACT_DELETE);
  }
}
