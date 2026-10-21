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

/** @brief Maximum interval between the clicks of a double click, in milliseconds. */
inline constexpr unsigned long DOUBLE_CLICK_MS = 400;

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

void handle_click(app_state &app, const ui_event *event){
  int px = event->x, py = event->y;
  int lx, ly, lw, lh;
  list_rect(app, &lx, &ly, &lw, &lh);

  if(event->type == ui_event_type::wheel){
    if(inside(px, py, lx, ly, lw, lh)){
      *list_scroll(app) += 3 * event->wheel;
      clamp_scroll(app);
    }
    return;
  }

  button b[MAX_BUTTONS];
  int count = get_buttons(app, b);
  for(int i = 0; i < count; i++)
    if(inside(px, py, b[i].x, b[i].y, b[i].w, b[i].h)){
      handle_action(app, b[i].action);
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
    int index = *list_scroll(app) + (py - ly) / ROW_H;
    if(index >= list_count(app)) return;

    bool double_click = index == app.last_click_index && event->time - app.last_click_time < DOUBLE_CLICK_MS;
    app.last_click_index = index;
    app.last_click_time = event->time;

    select_row(app, index);
    if(double_click){
      app.last_click_index = -1;
      activate_selection(app);
    }
  }
}

/**
 * @brief Appends a character to the focused field, or deletes its last character.
 * @param app Application state.
 * @param c     Character to append.
 * @param erase True to delete the last character instead.
 */
static void edit_field(app_state &app, char c, bool erase){
  if(app.focus == ui_field::path){
    if(erase && !app.path_field.empty()) app.path_field.pop_back();
    else if(!erase && app.path_field.size() < MAX_PATH_FIELD) app.path_field += c;
    return;
  }
  secret_string<MAX_PASSWORD> &field = app.focus == ui_field::password ? app.password_field : app.confirm_field;
  if(erase) field.pop_back();
  else field.push_back(c);
}

void handle_key(app_state &app, const ui_event *event){
  ui_key key = event->key;

  switch(key){
    case ui_key::up:        move_selection(app, -1); return;
    case ui_key::down:      move_selection(app, 1); return;
    case ui_key::page_up:   move_selection(app, -visible_rows(app)); return;
    case ui_key::page_down: move_selection(app, visible_rows(app)); return;
    case ui_key::home:      move_selection(app, -list_count(app)); return;
    case ui_key::end:       move_selection(app, list_count(app)); return;
    /* The other keys depend on the screen and are handled below. */
    default:                break;
  }

  if(app.screen == ui_screen::open){
    int focus = static_cast<int>(app.focus);
    if(key == ui_key::tab){ app.focus = static_cast<ui_field>((focus + 1) % UI_FIELD_COUNT); return; }
    if(key == ui_key::back_tab){ app.focus = static_cast<ui_field>((focus + UI_FIELD_COUNT - 1) % UI_FIELD_COUNT); return; }
    if(key == ui_key::enter){
      handle_action(app, app.focus == ui_field::confirm ? ui_action::create : ui_action::open);
      return;
    }
    if(key == ui_key::backspace){ edit_field(app, 0, true); return; }
    if(key == ui_key::character) edit_field(app, event->ch, false);
    return;
  }

  if(key == ui_key::enter){
    activate_selection(app);
  }else if(key == ui_key::escape && app.screen != ui_screen::archive){
    handle_action(app, ui_action::pick_cancel);
  }else if(key == ui_key::backspace && app.screen != ui_screen::archive && !fs_is_root(app.files.dir())){
    app.files.enter("..");
  }else if(key == ui_key::del && app.screen == ui_screen::archive){
    handle_action(app, ui_action::remove);
  }
}

}
