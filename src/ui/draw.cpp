/**
 * @file    draw.cpp
 * @brief   Painting of each screen into the off-screen buffer.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.h"

#include <stdio.h>
#include <string.h>
#include "core/fs.h"
#include "core/status.h"
#include "core/util.h"

/* ---------------- Primitives ---------------- */

/**
 * @brief Fills a rectangle with a solid color.
 * @param x     Left coordinate.
 * @param y     Top coordinate.
 * @param w     Width.
 * @param h     Height.
 * @param color Fill color (0xRRGGBB).
 */
static void fill(int x, int y, int w, int h, unsigned long color){
  platform_fill_rect(x, y, w, h, color);
}

/**
 * @brief Draws a string.
 * @param f        FONT_* value.
 * @param x        Left coordinate.
 * @param baseline Baseline coordinate.
 * @param s        String.
 * @param color    Text color (0xRRGGBB).
 */
static void draw_text(int f, int x, int baseline, const char *s, unsigned long color){
  platform_draw_text(f, x, baseline, s, (int) strlen(s), color);
}

/**
 * @brief Draws a string truncated with an ellipsis to fit in max_w.
 * @param f         FONT_* value.
 * @param x         Left coordinate.
 * @param baseline  Baseline coordinate.
 * @param max_w     Maximum width in pixels.
 * @param s         String.
 * @param keep_tail True to keep the end (paths), false to keep the start (names).
 * @param color     Text color (0xRRGGBB).
 */
static void draw_text_fit(int f, int x, int baseline, int max_w, const char *s,
                          bool keep_tail, unsigned long color){
  int len = (int) strlen(s);
  if(platform_text_width(f, s, len) <= max_w){
    draw_text(f, x, baseline, s, color);
    return;
  }

  char out[PATH_MAX + 4];
  int keep = len;
  while(keep > 0){
    keep--;
    if(keep_tail) snprintf(out, sizeof(out), "...%s", s + len - keep);
    else snprintf(out, sizeof(out), "%.*s...", keep, s);
    if(platform_text_width(f, out, (int) strlen(out)) <= max_w) break;
  }
  draw_text(f, x, baseline, out, color);
}

/**
 * @brief  Measures the width of a string.
 * @param  f FONT_* value.
 * @param  s String.
 * @return Width in pixels.
 */
static int text_width(int f, const char *s){
  return platform_text_width(f, s, (int) strlen(s));
}

/* ---------------- Widgets ---------------- */

/**
 * @brief Draws the list of the current screen with its scroll bar.
 */
static void draw_list(void){
  int x, y, w, h;
  list_rect(&x, &y, &w, &h);
  fill(x, y, w, h, COLOR_PANEL);

  int count = list_count(), sel = *list_sel(), scroll = *list_scroll(), rows = visible_rows();
  int text_y = (ROW_H + platform_font_ascent(FONT_REGULAR) - platform_font_descent(FONT_REGULAR)) / 2;

  if(count == 0){
    const char *hint = app.screen == SCREEN_ARCHIVE ? "The .crls is empty. Use \"Add\"."
                                                    : "(empty folder)";
    draw_text(FONT_REGULAR, x + 12, y + text_y, hint, COLOR_MUTED);
  }

  for(int r = 0; r < rows && scroll + r < count; r++){
    int i = scroll + r, row_y = y + r*ROW_H;
    if(i == sel) fill(x, row_y, w, ROW_H, COLOR_SELECTED);
    else if(r % 2) fill(x, row_y, w, ROW_H, COLOR_ROW_ALT);

    if(app.screen == SCREEN_ARCHIVE){
      archive_entry *e = archive_get_entry(&app.arc, (unsigned int) i);
      char size_text[32];
      format_size(e->size, size_text, sizeof(size_text));
      int size_w = text_width(FONT_REGULAR, size_text);
      draw_text_fit(FONT_REGULAR, x + 12, row_y + text_y, w - size_w - 48, e->name, false, COLOR_TEXT);
      draw_text(FONT_REGULAR, x + w - size_w - 20, row_y + text_y, size_text, COLOR_MUTED);
    }else{
      dir_item *item = browser_get_item(&app.files, (unsigned int) i);
      char label[MAX_NAME + 2];
      snprintf(label, sizeof(label), item->is_dir ? "%s/" : "%s", item->name);
      unsigned long color = item->is_dir ? COLOR_DIR : (has_crls_ext(item->name) ? COLOR_FILE : COLOR_TEXT);
      draw_text_fit(FONT_REGULAR, x + 12, row_y + text_y, w - 32, label, false, color);
    }
  }

  if(count > rows){
    int bar_h = h * rows / count;
    if(bar_h < 20) bar_h = 20;
    int bar_y = y + (h - bar_h) * scroll / (count - rows);
    fill(x + w - 6, bar_y, 4, bar_h, COLOR_BORDER);
  }
}

/**
 * @brief Draws a labeled text field of SCREEN_OPEN.
 * @param index  FIELD_* value.
 * @param label  Label displayed to the left.
 * @param value  Field content.
 * @param secret True to mask the content with asterisks.
 */
static void draw_field(int index, const char *label, const char *value, bool secret){
  int x, y, w, h;
  field_rect(index, &x, &y, &w, &h);
  int text_y = y + (h + platform_font_ascent(FONT_REGULAR) - platform_font_descent(FONT_REGULAR)) / 2;

  draw_text(FONT_REGULAR, MARGIN, text_y, label, COLOR_TEXT);
  fill(x, y, w, h, COLOR_PANEL);
  platform_draw_rect(x, y, w, h, app.focus == index ? COLOR_FOCUS : COLOR_BORDER);

  char shown[PATH_MAX];
  if(secret){
    size_t len = strlen(value);
    memset(shown, '*', len);
    shown[len] = '\0';
  }else{
    snprintf(shown, sizeof(shown), "%s", value);
  }
  draw_text_fit(FONT_REGULAR, x + 8, text_y, w - 24, shown, true, COLOR_TEXT);

  if(app.focus == index){
    int len = (int) strlen(shown);
    int cursor_x = x + 8 + platform_text_width(FONT_REGULAR, shown, len);
    if(cursor_x > x + w - 14) cursor_x = x + w - 14;
    fill(cursor_x + 1, y + 6, 2, h - 12, COLOR_FOCUS);
  }
}

/**
 * @brief Draws the buttons of the current screen.
 */
static void draw_buttons(void){
  button b[MAX_BUTTONS];
  int count = get_buttons(b);
  for(int i = 0; i < count; i++){
    fill(b[i].x, b[i].y, b[i].w, b[i].h, b[i].color);
    unsigned long text_color = b[i].color == COLOR_BUTTON ? COLOR_TEXT : COLOR_DARK_TEXT;
    int tx = b[i].x + (b[i].w - text_width(FONT_BOLD, b[i].label)) / 2;
    int ty = b[i].y + (b[i].h + platform_font_ascent(FONT_BOLD) - platform_font_descent(FONT_BOLD)) / 2;
    draw_text(FONT_BOLD, tx, ty, b[i].label, text_color);
  }
}

/* ---------------- Screens ---------------- */

/**
 * @brief Draws the path of the browsed folder below the title.
 */
static void draw_folder_line(void){
  char line[PATH_MAX + 16];
  snprintf(line, sizeof(line), "Folder: %s", app.files.dir);
  draw_text_fit(FONT_REGULAR, MARGIN, 60, platform_width() - 2*MARGIN, line, true, COLOR_MUTED);
}

/**
 * @brief Draws SCREEN_OPEN.
 */
static void draw_open_screen(void){
  draw_text(FONT_TITLE, MARGIN, 34, ".crls archive manager", COLOR_TEXT);
  draw_folder_line();
  draw_list();
  draw_field(FIELD_PATH, ".crls file or URL", app.path_field, false);
  draw_field(FIELD_PASSWORD, "Password", app.password_field, true);
  draw_field(FIELD_CONFIRM, "Confirm (create)", app.confirm_field, true);
}

/**
 * @brief Draws SCREEN_ARCHIVE.
 */
static void draw_archive_screen(void){
  archive *a = &app.arc;
  draw_text_fit(FONT_TITLE, MARGIN, 34, platform_width() - 2*MARGIN, fs_basename(a->path), false, COLOR_TEXT);
  draw_text_fit(FONT_REGULAR, MARGIN, 58, platform_width() - 2*MARGIN, a->path, true, COLOR_MUTED);

  uint64_t wasted = archive_wasted_space(a);
  char size_text[32], wasted_text[32], line[160];
  format_size(archive_total_size(a), size_text, sizeof(size_text));
  format_size(wasted, wasted_text, sizeof(wasted_text));
  snprintf(line, sizeof(line), "%zu file%s, %s", a->entries.size(), a->entries.size() == 1 ? "" : "s", size_text);
  if(a->is_remote) snprintf(line + strlen(line), sizeof(line) - strlen(line), "  |  remote: Add uploads");
  else if(wasted) snprintf(line + strlen(line), sizeof(line) - strlen(line), "  |  %s unused (Compact)", wasted_text);
  draw_text(FONT_REGULAR, MARGIN, 80, line, COLOR_MUTED);
  draw_list();
}

/**
 * @brief Draws SCREEN_PICK_ADD and SCREEN_PICK_EXTRACT.
 */
static void draw_pick_screen(void){
  const char *title = app.screen == SCREEN_PICK_ADD ? "Choose the file to add"
                    : app.extract_all ? "Choose the folder to extract everything into"
                    : "Choose the folder to extract into";
  draw_text(FONT_TITLE, MARGIN, 34, title, COLOR_TEXT);
  draw_folder_line();
  draw_list();
}

void draw(void){
  fill(0, 0, platform_width(), platform_height(), COLOR_BG);

  if(app.screen == SCREEN_OPEN) draw_open_screen();
  else if(app.screen == SCREEN_ARCHIVE) draw_archive_screen();
  else draw_pick_screen();

  draw_buttons();
  draw_text_fit(FONT_REGULAR, MARGIN, status_y(), platform_width() - 2*MARGIN, status_text, false,
                status_error ? COLOR_ERROR : COLOR_OK);

  platform_present();
}
