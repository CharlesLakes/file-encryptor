/**
 * @file    input.cpp
 * @brief   Translation of mouse and keyboard events into actions.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.hpp"

#include <algorithm>
#include "core/fs.hpp"

namespace crls{

/** @brief Maximum interval between the clicks of a double click, in milliseconds. */
inline constexpr unsigned long DOUBLE_CLICK_MS = 400;

void handle_click(app_state &app, const ui_event &event){
  int px = event.x, py = event.y;
  rect list = list_rect(app);

  if(event.type == ui_event_type::wheel){
    if(list.contains(px, py)){
      list_scroll(app) += 3 * event.wheel;
      clamp_scroll(app);
    }
    return;
  }

  button_row row = get_buttons(app);
  std::span<const button> buttons = row.list();
  auto hit = std::ranges::find_if(buttons, [&](const button &b){ return b.area.contains(px, py); });
  if(hit != buttons.end()){
    handle_action(app, hit->action);
    return;
  }

  if(app.screen == ui_screen::open){
    for(ui_field field : UI_FIELDS){
      if(field_rect(field).contains(px, py)){
        app.focus = field;
        return;
      }
    }
  }

  if(list.contains(px, py)){
    int index = list_scroll(app) + (py - list.y) / ROW_H;
    if(index >= list_count(app)) return;

    bool double_click = index == app.last_click_index && event.time - app.last_click_time < DOUBLE_CLICK_MS;
    app.last_click_index = index;
    app.last_click_time = event.time;

    select_row(app, index);
    if(double_click){
      app.last_click_index = -1;
      activate_selection(app);
    }
  }
}

/**
 * @brief Appends a character to the focused field, or deletes its last character.
 * @param app   Application state.
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

/**
 * @brief  Returns the field that follows (or precedes) another in focus order,
 *         wrapping around.
 * @param  field Current field.
 * @param  step  1 for the next field, -1 for the previous one.
 * @return The new field.
 */
static ui_field cycle_field(ui_field field, int step){
  int count = static_cast<int>(UI_FIELDS.size());
  return UI_FIELDS[static_cast<std::size_t>((static_cast<int>(field) + step + count) % count)];
}

void handle_key(app_state &app, const ui_event &event){
  ui_key key = event.key;

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
    if(key == ui_key::tab) app.focus = cycle_field(app.focus, 1);
    else if(key == ui_key::back_tab) app.focus = cycle_field(app.focus, -1);
    else if(key == ui_key::enter) handle_action(app, app.focus == ui_field::confirm ? ui_action::create : ui_action::open);
    else if(key == ui_key::backspace) edit_field(app, 0, true);
    else if(key == ui_key::character) edit_field(app, event.ch, false);
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
