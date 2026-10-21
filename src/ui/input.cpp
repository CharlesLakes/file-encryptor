/**
 * @file    input.cpp
 * @brief   Translation of mouse and keyboard events into actions.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.h"

#include <string.h>
#include "core/fs.h"

namespace crls{

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

  if(app.screen == ui_screen::open)
    for(int i = 0; i < UI_FIELD_COUNT; i++){
      ui_field field = static_cast<ui_field>(i);
      int fx, fy, fw, fh;
      field_rect(field, &fx, &fy, &fw, &fh);
      if(inside(px, py, fx, fy, fw, fh)){
        app.focus = field;
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
  char *field = app.focus == ui_field::path ? app.path_field
              : app.focus == ui_field::password ? app.password_field : app.confirm_field;
  size_t max = app.focus == ui_field::path ? sizeof(app.path_field) - 1 : MAX_PASSWORD;
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

  if(app.screen == ui_screen::open){
    int focus = static_cast<int>(app.focus);
    if(key == KEY_TAB){ app.focus = static_cast<ui_field>((focus + 1) % UI_FIELD_COUNT); return; }
    if(key == KEY_BACK_TAB){ app.focus = static_cast<ui_field>((focus + UI_FIELD_COUNT - 1) % UI_FIELD_COUNT); return; }
    if(key == KEY_ENTER){
      handle_action(app.focus == ui_field::confirm ? ui_action::create : ui_action::open);
      return;
    }
    if(key == KEY_BACKSPACE){ edit_field(0, true); return; }
    if(key == KEY_CHAR) edit_field(event->ch, false);
    return;
  }

  if(key == KEY_ENTER){
    activate_selection();
  }else if(key == KEY_ESCAPE && app.screen != ui_screen::archive){
    handle_action(ui_action::pick_cancel);
  }else if(key == KEY_BACKSPACE && app.screen != ui_screen::archive && !fs_is_root(app.files.dir().c_str())){
    app.files.enter("..");
  }else if(key == KEY_DELETE && app.screen == ui_screen::archive){
    handle_action(ui_action::remove);
  }
}

}
