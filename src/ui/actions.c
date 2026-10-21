/**
 * @file    actions.c
 * @brief   Behavior of each button and key.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "core/status.h"
#include "core/util.h"
#include "deps/sodium.h"

/* ---------------- Helpers ---------------- */

void clear_secrets(void){
  sodium_memzero(app.password_field, sizeof(app.password_field));
  sodium_memzero(app.confirm_field, sizeof(app.confirm_field));
}

/**
 * @brief Resolves the path field, which may be absolute or relative to the
 *        browsed folder.
 * @param out      Destination buffer.
 * @param out_size Capacity of out.
 */
static void resolve_field_path(char *out, size_t out_size){
  if(app.path_field[0] == '/') snprintf(out, out_size, "%s", app.path_field);
  else join_path(out, out_size, app.browser.dir, app.path_field);
}

/**
 * @brief Switches to another screen. Browsing screens reload the folder with
 *        the given filter.
 * @param new_screen SCREEN_* value.
 * @param filter     FILTER_* value used by browsing screens.
 */
static void go_to(int new_screen, int filter){
  app.screen = new_screen;
  if(new_screen != SCREEN_ARCHIVE){
    app.browser.filter = filter;
    browser_load(&app.browser);
  }
  clamp_scroll();
}

/**
 * @brief On SCREEN_OPEN, copies the path of the selected file to the path field.
 */
static void on_browser_select(void){
  if(app.screen != SCREEN_OPEN || app.browser.sel < 0) return;
  dir_item *item = browser_get_item(&app.browser, (unsigned int) app.browser.sel);
  if(!item->is_dir) join_path(app.path_field, sizeof(app.path_field), app.browser.dir, item->name);
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
    app.focus = FIELD_PASSWORD;
    return;
  }

  char path[PATH_MAX];
  resolve_field_path(path, sizeof(path));
  set_status(false, "Deriving the key...");
  draw();

  if(!archive_open(&app.archive, path, app.password_field)) return;
  clear_secrets();
  unsigned int n = app.archive.entries.n;
  app.entry_sel = n ? 0 : -1;
  app.entry_scroll = 0;
  go_to(SCREEN_ARCHIVE, app.browser.filter);
  set_status(false, "Archive opened: %u file%s.", n, n == 1 ? "" : "s");
}

/**
 * @brief Creates a new archive from the path, password and confirmation fields.
 */
static void create_archive(void){
  if(app.path_field[0] == '\0'){
    set_status(true, "Type the name of the new .crls file.");
    app.focus = FIELD_PATH;
    return;
  }

  char path[PATH_MAX];
  resolve_field_path(path, sizeof(path));
  if(!has_crls_ext(path)) strncat(path, ".crls", sizeof(path) - strlen(path) - 1);

  if(strlen(app.password_field) < MIN_PASSWORD){
    set_status(true, "The password must have at least %d characters.", MIN_PASSWORD);
    app.focus = FIELD_PASSWORD;
    return;
  }
  if(strcmp(app.password_field, app.confirm_field) != 0){
    set_status(true, "The confirmation does not match the password.");
    app.focus = FIELD_CONFIRM;
    return;
  }
  if(access(path, F_OK) == 0){
    set_status(true, "%s already exists. Press Open to use it.", path);
    return;
  }

  set_status(false, "Deriving the key...");
  draw();

  if(!archive_create(&app.archive, path, app.password_field)) return;
  clear_secrets();
  app.entry_sel = -1;
  app.entry_scroll = 0;
  go_to(SCREEN_ARCHIVE, app.browser.filter);
  set_status(false, "Created %s.", app.archive.path);
}

/**
 * @brief Extracts the selected entry, or all entries, into the browsed folder.
 */
static void extract_to_current_dir(void){
  unsigned int from = app.extract_all ? 0 : (unsigned int) app.entry_sel;
  unsigned int to = app.extract_all ? app.archive.entries.n : (unsigned int) app.entry_sel + 1;
  int done = 0, skipped = 0, failed = 0;

  for(unsigned int i = from; i < to; i++){
    int result = archive_extract_entry(&app.archive, i, app.browser.dir);
    if(result > 0) done++;
    else if(result == 0) skipped++;
    else failed++;
  }

  if(failed) return;
  if(skipped) set_status(true, "Extracted %d. Skipped %d that already existed in %s.", done, skipped, app.browser.dir);
  else set_status(false, "Extracted %d file%s into %s.", done, done == 1 ? "" : "s", app.browser.dir);
}

/**
 * @brief Deletes the selected entry. The first call only requests confirmation.
 */
static void delete_selected(void){
  if(app.entry_sel < 0){
    set_status(true, "Select the file you want to delete.");
  }else if(app.confirm_delete != app.entry_sel){
    app.confirm_delete = app.entry_sel;
    set_status(true, "Press Delete again to remove %s from the .crls.",
               archive_get_entry(&app.archive, (unsigned int) app.entry_sel)->name);
  }else{
    app.confirm_delete = -1;
    if(archive_delete_entry(&app.archive, (unsigned int) app.entry_sel)){
      if(app.entry_sel >= (int) app.archive.entries.n) app.entry_sel = (int) app.archive.entries.n - 1;
      clamp_scroll();
    }
  }
}

/**
 * @brief Compacts the archive if it contains unused space.
 */
static void compact(void){
  uint64_t wasted = archive_wasted_space(&app.archive);
  if(wasted == 0){
    set_status(false, "There is no unused space.");
    return;
  }
  set_status(false, "Compacting...");
  draw();
  char wasted_text[32];
  format_size(wasted, wasted_text, sizeof(wasted_text));
  if(archive_compact(&app.archive)) set_status(false, "Compacted: reclaimed %s.", wasted_text);
}

/**
 * @brief Confirms the browsing screen: "Add" on SCREEN_PICK_ADD or
 *        "Extract here" on SCREEN_PICK_EXTRACT.
 */
static void pick_confirm(void){
  if(app.screen == SCREEN_PICK_EXTRACT){
    set_status(false, "Extracting...");
    draw();
    extract_to_current_dir();
    app.screen = SCREEN_ARCHIVE;
    return;
  }

  if(app.browser.sel < 0){
    set_status(true, "Select a file.");
    return;
  }
  dir_item *item = browser_get_item(&app.browser, (unsigned int) app.browser.sel);
  if(item->is_dir){
    browser_enter(&app.browser, item->name);
    return;
  }
  char path[PATH_MAX];
  join_path(path, sizeof(path), app.browser.dir, item->name);
  set_status(false, "Adding %s...", item->name);
  draw();
  if(archive_add_file(&app.archive, path)){
    app.entry_sel = (int) app.archive.entries.n - 1;
    app.screen = SCREEN_ARCHIVE;
    ensure_visible();
  }
}

void handle_action(int action){
  if(action != ACT_DELETE) app.confirm_delete = -1;

  switch(action){
    case ACT_OPEN:
      open_archive();
      break;
    case ACT_CREATE:
      create_archive();
      break;
    case ACT_ADD:
      go_to(SCREEN_PICK_ADD, FILTER_ALL);
      set_status(false, "");
      break;
    case ACT_EXTRACT:
    case ACT_EXTRACT_ALL:
      if(action == ACT_EXTRACT && app.entry_sel < 0){
        set_status(true, "Select the file you want to extract.");
        break;
      }
      if(app.archive.entries.n == 0){
        set_status(true, "There are no files to extract.");
        break;
      }
      app.extract_all = action == ACT_EXTRACT_ALL;
      go_to(SCREEN_PICK_EXTRACT, FILTER_DIRS);
      set_status(false, "");
      break;
    case ACT_DELETE:
      delete_selected();
      break;
    case ACT_COMPACT:
      compact();
      break;
    case ACT_CLOSE:
      archive_close(&app.archive);
      go_to(SCREEN_OPEN, FILTER_CRLS);
      set_status(false, "Archive closed.");
      break;
    case ACT_PICK_CONFIRM:
      pick_confirm();
      break;
    case ACT_PICK_CANCEL:
      app.screen = SCREEN_ARCHIVE;
      set_status(false, "");
      break;
  }
}

/* ---------------- List selection ---------------- */

void activate_selection(void){
  int sel = *list_sel();
  if(sel < 0) return;

  if(app.screen == SCREEN_ARCHIVE){
    handle_action(ACT_EXTRACT);
    return;
  }

  dir_item *item = browser_get_item(&app.browser, (unsigned int) sel);
  if(item->is_dir){
    browser_enter(&app.browser, item->name);
  }else if(app.screen == SCREEN_OPEN){
    on_browser_select();
    app.focus = FIELD_PASSWORD;
  }else if(app.screen == SCREEN_PICK_ADD){
    handle_action(ACT_PICK_CONFIRM);
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
