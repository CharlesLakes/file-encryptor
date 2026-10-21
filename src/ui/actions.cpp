/**
 * @file    actions.cpp
 * @brief   Behavior of each button and key.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "core/fs.h"
#include "core/remote.h"
#include "core/status.h"
#include "core/util.h"
#include "deps/sodium.h"

namespace crls{

/* ---------------- Helpers ---------------- */

void clear_secrets(void){
  sodium_memzero(app.password_field, sizeof(app.password_field));
  sodium_memzero(app.confirm_field, sizeof(app.confirm_field));
}

/**
 * @brief Resolves the path field, which may be a URL, an absolute path or a
 *        path relative to the browsed folder.
 * @param out      Destination buffer.
 * @param out_size Capacity of out.
 */
static void resolve_field_path(char *out, size_t out_size){
  if(remote_is_url(app.path_field) || fs_is_absolute(app.path_field)) snprintf(out, out_size, "%s", app.path_field);
  else join_path(out, out_size, app.files.dir().c_str(), app.path_field);
}

/**
 * @brief Switches to another screen. Browsing screens reload the folder with
 *        the given filter.
 * @param new_screen Screen to show.
 * @param filter     Filter used by browsing screens.
 */
static void go_to(ui_screen new_screen, browser_filter filter){
  app.screen = new_screen;
  if(new_screen != ui_screen::archive){
    app.files.filter = filter;
    app.files.load();
  }
  clamp_scroll();
}

/**
 * @brief On ui_screen::open, copies the path of the selected file to the path field.
 */
static void on_browser_select(void){
  if(app.screen != ui_screen::open || app.files.sel < 0) return;
  const dir_item *item = &app.files.items()[app.files.sel];
  if(!item->is_dir) join_path(app.path_field, sizeof(app.path_field), app.files.dir().c_str(), item->name.c_str());
}

/* ---------------- Actions ---------------- */

/**
 * @brief Opens the archive given by the path and password fields.
 */
static void open_archive(void){
  if(app.path_field[0] == '\0'){
    set_status(true, "Choose or type the path of a .crls file.");
    return;
  }
  if(app.password_field[0] == '\0'){
    set_status(true, "Type the password.");
    app.focus = ui_field::password;
    return;
  }

  char path[PATH_MAX];
  resolve_field_path(path, sizeof(path));
  bool remote = remote_is_url(path);
  set_status(false, remote ? "Downloading the index and deriving the key..." : "Deriving the key...");
  draw();

  if(remote ? !remote_open(app.arc, path, app.password_field)
            : !app.arc.open(path, app.password_field)) return;
  clear_secrets();
  unsigned int n = app.arc.entries().size();
  app.entry_sel = n ? 0 : -1;
  app.entry_scroll = 0;
  go_to(ui_screen::archive, app.files.filter);
  set_status(false, "Archive opened: %u file%s.", n, n == 1 ? "" : "s");
}

/**
 * @brief Creates a new archive from the path, password and confirmation fields.
 */
static void create_archive(void){
  if(app.path_field[0] == '\0'){
    set_status(true, "Type the name of the new .crls file.");
    app.focus = ui_field::path;
    return;
  }

  if(remote_is_url(app.path_field)){
    set_status(true, "Create the archive here, then copy it to the server.");
    return;
  }

  char path[PATH_MAX];
  resolve_field_path(path, sizeof(path));
  if(!has_crls_ext(path)) strncat(path, ".crls", sizeof(path) - strlen(path) - 1);

  if(strlen(app.password_field) < MIN_PASSWORD){
    set_status(true, "The password must have at least %zu characters.", MIN_PASSWORD);
    app.focus = ui_field::password;
    return;
  }
  if(strcmp(app.password_field, app.confirm_field) != 0){
    set_status(true, "The confirmation does not match the password.");
    app.focus = ui_field::confirm;
    return;
  }
  if(access(path, F_OK) == 0){
    set_status(true, "%s already exists. Press Open to use it.", path);
    return;
  }

  set_status(false, "Deriving the key...");
  draw();

  if(!app.arc.create(path, app.password_field)) return;
  clear_secrets();
  app.entry_sel = -1;
  app.entry_scroll = 0;
  go_to(ui_screen::archive, app.files.filter);
  set_status(false, "Created %s.", app.arc.path().c_str());
}

/**
 * @brief Extracts the selected entry, or all entries, into the browsed folder.
 */
static void extract_to_current_dir(void){
  unsigned int from = app.extract_all ? 0 : (unsigned int) app.entry_sel;
  unsigned int to = app.extract_all ? app.arc.entries().size() : (unsigned int) app.entry_sel + 1;
  int done = 0, skipped = 0, failed = 0;

  for(unsigned int i = from; i < to; i++){
    extract_result result = app.arc.extract(i, app.files.dir().c_str());
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
 */
static void delete_selected(void){
  if(app.arc.is_remote()){
    set_status(true, "Files cannot be deleted from a remote archive.");
  }else if(app.entry_sel < 0){
    set_status(true, "Select the file you want to delete.");
  }else if(app.confirm_delete != app.entry_sel){
    app.confirm_delete = app.entry_sel;
    set_status(true, "Press Delete again to remove %s from the .crls.",
               app.arc.entries()[app.entry_sel].name.c_str());
  }else{
    app.confirm_delete = -1;
    if(app.arc.remove((unsigned int) app.entry_sel)){
      if(app.entry_sel >= (int) app.arc.entries().size()) app.entry_sel = (int) app.arc.entries().size() - 1;
      clamp_scroll();
    }
  }
}

/**
 * @brief Compacts the archive if it contains unused space.
 */
static void compact(void){
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
  draw();
  char wasted_text[32];
  format_size(wasted, wasted_text, sizeof(wasted_text));
  if(app.arc.compact()) set_status(false, "Compacted: reclaimed %s.", wasted_text);
  else if(!app.arc.is_open()) app.screen = ui_screen::open;
}

/**
 * @brief Confirms the browsing screen: "Add" on ui_screen::pick_add or
 *        "Extract here" on ui_screen::pick_extract.
 */
static void pick_confirm(void){
  if(app.screen == ui_screen::pick_extract){
    set_status(false, "Extracting...");
    draw();
    extract_to_current_dir();
    app.screen = ui_screen::archive;
    return;
  }

  if(app.files.sel < 0){
    set_status(true, "Select a file.");
    return;
  }
  const dir_item *item = &app.files.items()[app.files.sel];
  if(item->is_dir){
    app.files.enter(item->name);
    return;
  }
  char path[PATH_MAX];
  join_path(path, sizeof(path), app.files.dir().c_str(), item->name.c_str());
  bool remote = app.arc.is_remote();
  set_status(false, remote ? "Encrypting and uploading %s..." : "Adding %s...", item->name.c_str());
  draw();
  if(remote ? remote_add_file(app.arc, path) : app.arc.add_file(path)){
    app.entry_sel = (int) app.arc.entries().size() - 1;
    app.screen = ui_screen::archive;
    ensure_visible();
  }
}

void handle_action(ui_action action){
  if(action != ui_action::remove) app.confirm_delete = -1;

  switch(action){
    case ui_action::none:
      break;
    case ui_action::open:
      open_archive();
      break;
    case ui_action::create:
      create_archive();
      break;
    case ui_action::add:
      go_to(ui_screen::pick_add, browser_filter::all);
      clear_status();
      break;
    case ui_action::extract:
    case ui_action::extract_all:
      if(action == ui_action::extract && app.entry_sel < 0){
        set_status(true, "Select the file you want to extract.");
        break;
      }
      if(app.arc.entries().size() == 0){
        set_status(true, "There are no files to extract.");
        break;
      }
      app.extract_all = action == ui_action::extract_all;
      go_to(ui_screen::pick_extract, browser_filter::dirs);
      clear_status();
      break;
    case ui_action::remove:
      delete_selected();
      break;
    case ui_action::compact:
      compact();
      break;
    case ui_action::close:
      app.arc.close();
      go_to(ui_screen::open, browser_filter::crls);
      set_status(false, "Archive closed.");
      break;
    case ui_action::pick_confirm:
      pick_confirm();
      break;
    case ui_action::pick_cancel:
      app.screen = ui_screen::archive;
      clear_status();
      break;
  }
}

/* ---------------- List selection ---------------- */

void activate_selection(void){
  int sel = *list_sel();
  if(sel < 0) return;

  if(app.screen == ui_screen::archive){
    handle_action(ui_action::extract);
    return;
  }

  const dir_item *item = &app.files.items()[sel];
  if(item->is_dir){
    app.files.enter(item->name);
  }else if(app.screen == ui_screen::open){
    on_browser_select();
    app.focus = ui_field::password;
  }else if(app.screen == ui_screen::pick_add){
    handle_action(ui_action::pick_confirm);
  }
}

void move_selection(int delta){
  int count = list_count();
  if(count == 0) return;
  int *sel = list_sel();
  *sel += delta;
  if(*sel < 0) *sel = 0;
  if(*sel >= count) *sel = count - 1;
  app.confirm_delete = -1;
  ensure_visible();
  on_browser_select();
}

void select_row(int index){
  if(*list_sel() != index) app.confirm_delete = -1;
  *list_sel() = index;
  on_browser_select();
}

}
