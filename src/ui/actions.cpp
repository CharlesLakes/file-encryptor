/**
 * @file    actions.cpp
 * @brief   Behavior of each button and key.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.h"

#include <algorithm>
#include "core/fs.h"
#include "core/remote.h"
#include "core/status.h"
#include "core/util.h"

namespace crls{

/* ---------------- Helpers ---------------- */

void clear_secrets(app_state &app){
  app.password_field.wipe();
  app.confirm_field.wipe();
}

/**
 * @brief  Resolves the path field, which may be a URL, an absolute path or a
 *         path relative to the browsed folder.
 * @param  app Application state.
 * @return The URL or path.
 */
static std::string resolve_field_path(const app_state &app){
  if(remote_is_url(app.path_field) || fs_is_absolute(app.path_field)) return app.path_field;
  return join_path(app.files.dir(), app.path_field);
}

/**
 * @brief Switches to another screen. Browsing screens reload the folder with
 *        the given filter.
 * @param app Application state.
 * @param new_screen Screen to show.
 * @param filter     Filter used by browsing screens.
 */
static void go_to(app_state &app, ui_screen new_screen, browser_filter filter){
  app.screen = new_screen;
  if(new_screen != ui_screen::archive){
    app.files.filter = filter;
    app.files.load();
  }
  clamp_scroll(app);
}

/**
 * @brief On ui_screen::open, copies the path of the selected file to the path field.
 * @param app Application state.
 */
static void on_browser_select(app_state &app){
  if(app.screen != ui_screen::open || app.files.sel < 0) return;
  const dir_item &item = app.files.items()[static_cast<std::size_t>(app.files.sel)];
  if(!item.is_dir) app.path_field = join_path(app.files.dir(), item.name);
}

/* ---------------- Actions ---------------- */

/**
 * @brief Opens the archive given by the path and password fields.
 * @param app Application state.
 */
static void open_archive(app_state &app){
  if(app.path_field.empty()){
    set_status(true, "Choose or type the path of a .crls file.");
    return;
  }
  if(app.password_field.empty()){
    set_status(true, "Type the password.");
    app.focus = ui_field::password;
    return;
  }

  std::string path = resolve_field_path(app);
  bool remote = remote_is_url(path);
  set_status(false, remote ? "Downloading the index and deriving the key..." : "Deriving the key...");
  draw(app);

  if(remote ? !remote_open(app.arc, path, app.password_field.c_str())
            : !app.arc.open(path, app.password_field.c_str())) return;
  clear_secrets(app);
  std::size_t n = app.arc.entries().size();
  app.entry_sel = n ? 0 : -1;
  app.entry_scroll = 0;
  go_to(app, ui_screen::archive, app.files.filter);
  set_status(false, "Archive opened: %zu file%s.", n, n == 1 ? "" : "s");
}

/**
 * @brief Creates a new archive from the path, password and confirmation fields.
 * @param app Application state.
 */
static void create_archive(app_state &app){
  if(app.path_field.empty()){
    set_status(true, "Type the name of the new .crls file.");
    app.focus = ui_field::path;
    return;
  }

  if(remote_is_url(app.path_field)){
    set_status(true, "Create the archive here, then copy it to the server.");
    return;
  }

  std::string path = resolve_field_path(app);
  if(!has_crls_ext(path)) path += ".crls";

  if(app.password_field.size() < MIN_PASSWORD){
    set_status(true, "The password must have at least %zu characters.", MIN_PASSWORD);
    app.focus = ui_field::password;
    return;
  }
  if(app.password_field.view() != app.confirm_field.view()){
    set_status(true, "The confirmation does not match the password.");
    app.focus = ui_field::confirm;
    return;
  }
  if(fs_exists(path)){
    set_status(true, "%s already exists. Press Open to use it.", path.c_str());
    return;
  }

  set_status(false, "Deriving the key...");
  draw(app);

  if(!app.arc.create(path, app.password_field.c_str())) return;
  clear_secrets(app);
  app.entry_sel = -1;
  app.entry_scroll = 0;
  go_to(app, ui_screen::archive, app.files.filter);
  set_status(false, "Created %s.", app.arc.path().c_str());
}

/**
 * @brief Extracts the selected entry, or all entries, into the browsed folder.
 * @param app Application state.
 */
static void extract_to_current_dir(app_state &app){
  std::size_t from = app.extract_all ? 0 : static_cast<std::size_t>(app.entry_sel);
  std::size_t to = app.extract_all ? app.arc.entries().size() : from + 1;
  int done = 0, skipped = 0, failed = 0;

  for(std::size_t i = from; i < to; i++){
    extract_result result = app.arc.extract(i, app.files.dir());
    if(result == extract_result::extracted) done++;
    else if(result == extract_result::skipped) skipped++;
    else failed++;
  }

  if(failed) return;
  if(skipped) set_status(true, "Extracted %d. Skipped %d that already existed in %s.", done, skipped, app.files.dir().c_str());
  else set_status(false, "Extracted %d file%s into %s.", done, done == 1 ? "" : "s", app.files.dir().c_str());
}

/**
 * @brief Deletes the selected entry. The first call only requests confirmation.
 * @param app Application state.
 */
static void delete_selected(app_state &app){
  if(app.arc.is_remote()){
    set_status(true, "Files cannot be deleted from a remote archive.");
  }else if(app.entry_sel < 0){
    set_status(true, "Select the file you want to delete.");
  }else if(app.confirm_delete != app.entry_sel){
    app.confirm_delete = app.entry_sel;
    set_status(true, "Press Delete again to remove %s from the .crls.",
               app.arc.entries()[static_cast<std::size_t>(app.entry_sel)].name.c_str());
  }else{
    app.confirm_delete = -1;
    if(app.arc.remove(static_cast<std::size_t>(app.entry_sel))){
      app.entry_sel = std::min(app.entry_sel, static_cast<int>(app.arc.entries().size()) - 1);
      clamp_scroll(app);
    }
  }
}

/**
 * @brief Compacts the archive if it contains unused space.
 * @param app Application state.
 */
static void compact(app_state &app){
  if(app.arc.is_remote()){
    set_status(true, "A remote archive can only be compacted on the server.");
    return;
  }
  uint64_t wasted = app.arc.wasted_space();
  if(wasted == 0){
    set_status(false, "There is no unused space.");
    return;
  }
  set_status(false, "Compacting...");
  draw(app);
  if(app.arc.compact()) set_status(false, "Compacted: reclaimed %s.", format_size(wasted).c_str());
  else if(!app.arc.is_open()) app.screen = ui_screen::open;
}

/**
 * @brief Confirms the browsing screen: "Add" on ui_screen::pick_add or
 *        "Extract here" on ui_screen::pick_extract.
 * @param app Application state.
 */
static void pick_confirm(app_state &app){
  if(app.screen == ui_screen::pick_extract){
    set_status(false, "Extracting...");
    draw(app);
    extract_to_current_dir(app);
    app.screen = ui_screen::archive;
    return;
  }

  if(app.files.sel < 0){
    set_status(true, "Select a file.");
    return;
  }
  const dir_item &item = app.files.items()[static_cast<std::size_t>(app.files.sel)];
  if(item.is_dir){
    app.files.enter(item.name);
    return;
  }
  std::string path = join_path(app.files.dir(), item.name);
  bool remote = app.arc.is_remote();
  set_status(false, remote ? "Encrypting and uploading %s..." : "Adding %s...", item.name.c_str());
  draw(app);
  if(remote ? remote_add_file(app.arc, path) : app.arc.add_file(path)){
    app.entry_sel = static_cast<int>(app.arc.entries().size()) - 1;
    app.screen = ui_screen::archive;
    ensure_visible(app);
  }
}

void handle_action(app_state &app, ui_action action){
  if(action != ui_action::remove) app.confirm_delete = -1;

  switch(action){
    case ui_action::none:
      break;
    case ui_action::open:
      open_archive(app);
      break;
    case ui_action::create:
      create_archive(app);
      break;
    case ui_action::add:
      go_to(app, ui_screen::pick_add, browser_filter::all);
      clear_status();
      break;
    case ui_action::extract:
    case ui_action::extract_all:
      if(action == ui_action::extract && app.entry_sel < 0){
        set_status(true, "Select the file you want to extract.");
        break;
      }
      if(app.arc.entries().empty()){
        set_status(true, "There are no files to extract.");
        break;
      }
      app.extract_all = action == ui_action::extract_all;
      go_to(app, ui_screen::pick_extract, browser_filter::dirs);
      clear_status();
      break;
    case ui_action::remove:
      delete_selected(app);
      break;
    case ui_action::compact:
      compact(app);
      break;
    case ui_action::close:
      app.arc.close();
      go_to(app, ui_screen::open, browser_filter::crls);
      set_status(false, "Archive closed.");
      break;
    case ui_action::pick_confirm:
      pick_confirm(app);
      break;
    case ui_action::pick_cancel:
      app.screen = ui_screen::archive;
      clear_status();
      break;
  }
}

/* ---------------- List selection ---------------- */

void activate_selection(app_state &app){
  int sel = list_sel(app);
  if(sel < 0) return;

  if(app.screen == ui_screen::archive){
    handle_action(app, ui_action::extract);
    return;
  }

  const dir_item &item = app.files.items()[static_cast<std::size_t>(sel)];
  if(item.is_dir){
    app.files.enter(item.name);
  }else if(app.screen == ui_screen::open){
    on_browser_select(app);
    app.focus = ui_field::password;
  }else if(app.screen == ui_screen::pick_add){
    handle_action(app, ui_action::pick_confirm);
  }
}

void move_selection(app_state &app, int delta){
  int count = list_count(app);
  if(count == 0) return;
  int &sel = list_sel(app);
  sel = std::clamp(sel + delta, 0, count - 1);
  app.confirm_delete = -1;
  ensure_visible(app);
  on_browser_select(app);
}

void select_row(app_state &app, int index){
  int &sel = list_sel(app);
  if(sel != index) app.confirm_delete = -1;
  sel = index;
  on_browser_select(app);
}

}
