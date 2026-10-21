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

namespace crls{

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
 * @param f        Font.
 * @param x        Left coordinate.
 * @param baseline Baseline coordinate.
 * @param s        String.
 * @param color    Text color (0xRRGGBB).
 */
static void draw_text(ui_font f, int x, int baseline, const char *s, unsigned long color){
  platform_draw_text(f, x, baseline, s, (int) strlen(s), color);
}

/**
 * @brief Draws a string truncated with an ellipsis to fit in max_w.
 * @param f         Font.
 * @param x         Left coordinate.
 * @param baseline  Baseline coordinate.
 * @param max_w     Maximum width in pixels.
 * @param s         String.
 * @param keep_tail True to keep the end (paths), false to keep the start (names).
 * @param color     Text color (0xRRGGBB).
 */
static void draw_text_fit(ui_font f, int x, int baseline, int max_w, const char *s,
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
 * @param  f Font.
 * @param  s String.
 * @return Width in pixels.
 */
static int text_width(ui_font f, const char *s){
  return platform_text_width(f, s, (int) strlen(s));
}

/* ---------------- Widgets ---------------- */

/**
 * @brief Draws the list of the current screen with its scroll bar.
 * @param app Application state.
 */
static void draw_list(app_state &app){
  int x, y, w, h;
  list_rect(app, &x, &y, &w, &h);
  fill(x, y, w, h, COLOR_PANEL);

  int count = list_count(app), sel = *list_sel(app), scroll = *list_scroll(app), rows = visible_rows(app);
  int text_y = (ROW_H + platform_font_ascent(ui_font::regular) - platform_font_descent(ui_font::regular)) / 2;

  if(count == 0){
    const char *hint = app.screen == ui_screen::archive ? "The .crls is empty. Use \"Add\"."
                                                    : "(empty folder)";
    draw_text(ui_font::regular, x + 12, y + text_y, hint, COLOR_MUTED);
  }

  for(int r = 0; r < rows && scroll + r < count; r++){
    int i = scroll + r, row_y = y + r*ROW_H;
    if(i == sel) fill(x, row_y, w, ROW_H, COLOR_SELECTED);
    else if(r % 2) fill(x, row_y, w, ROW_H, COLOR_ROW_ALT);

    if(app.screen == ui_screen::archive){
      const archive_entry &e = app.arc.entries()[i];
      std::string size_text = format_size(e.size);
      int size_w = text_width(ui_font::regular, size_text.c_str());
      draw_text_fit(ui_font::regular, x + 12, row_y + text_y, w - size_w - 48, e.name.c_str(), false, COLOR_TEXT);
      draw_text(ui_font::regular, x + w - size_w - 20, row_y + text_y, size_text.c_str(), COLOR_MUTED);
    }else{
      const dir_item *item = &app.files.items()[i];
      char label[MAX_NAME + 2];
      snprintf(label, sizeof(label), item->is_dir ? "%s/" : "%s", item->name.c_str());
      unsigned long color = item->is_dir ? COLOR_DIR : (has_crls_ext(item->name) ? COLOR_FILE : COLOR_TEXT);
      draw_text_fit(ui_font::regular, x + 12, row_y + text_y, w - 32, label, false, color);
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
 * @brief Draws a labeled text field of ui_screen::open.
 * @param app Application state.
 * @param index  Field.
 * @param label  Label displayed to the left.
 * @param value  Field content.
 * @param secret True to mask the content with asterisks.
 */
static void draw_field(app_state &app, ui_field index, const char *label, const char *value, bool secret){
  int x, y, w, h;
  field_rect(index, &x, &y, &w, &h);
  int text_y = y + (h + platform_font_ascent(ui_font::regular) - platform_font_descent(ui_font::regular)) / 2;

  draw_text(ui_font::regular, MARGIN, text_y, label, COLOR_TEXT);
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
  draw_text_fit(ui_font::regular, x + 8, text_y, w - 24, shown, true, COLOR_TEXT);

  if(app.focus == index){
    int len = (int) strlen(shown);
    int cursor_x = x + 8 + platform_text_width(ui_font::regular, shown, len);
    if(cursor_x > x + w - 14) cursor_x = x + w - 14;
    fill(cursor_x + 1, y + 6, 2, h - 12, COLOR_FOCUS);
  }
}

/**
 * @brief Draws the buttons of the current screen.
 * @param app Application state.
 */
static void draw_buttons(app_state &app){
  button b[MAX_BUTTONS];
  int count = get_buttons(app, b);
  for(int i = 0; i < count; i++){
    fill(b[i].x, b[i].y, b[i].w, b[i].h, b[i].color);
    unsigned long text_color = b[i].color == COLOR_BUTTON ? COLOR_TEXT : COLOR_DARK_TEXT;
    int tx = b[i].x + (b[i].w - text_width(ui_font::bold, b[i].label)) / 2;
    int ty = b[i].y + (b[i].h + platform_font_ascent(ui_font::bold) - platform_font_descent(ui_font::bold)) / 2;
    draw_text(ui_font::bold, tx, ty, b[i].label, text_color);
  }
}

/* ---------------- Screens ---------------- */

/**
 * @brief Draws the path of the browsed folder below the title.
 * @param app Application state.
 */
static void draw_folder_line(app_state &app){
  char line[PATH_MAX + 16];
  snprintf(line, sizeof(line), "Folder: %s", app.files.dir().c_str());
  draw_text_fit(ui_font::regular, MARGIN, 60, platform_width() - 2*MARGIN, line, true, COLOR_MUTED);
}

/**
 * @brief Draws ui_screen::open.
 * @param app Application state.
 */
static void draw_open_screen(app_state &app){
  draw_text(ui_font::title, MARGIN, 34, ".crls archive manager", COLOR_TEXT);
  draw_folder_line(app);
  draw_list(app);
  draw_field(app, ui_field::path, ".crls file or URL", app.path_field, false);
  draw_field(app, ui_field::password, "Password", app.password_field, true);
  draw_field(app, ui_field::confirm, "Confirm (create)", app.confirm_field, true);
}

/**
 * @brief Draws ui_screen::archive.
 * @param app Application state.
 */
static void draw_archive_screen(app_state &app){
  archive *a = &app.arc;
  draw_text_fit(ui_font::title, MARGIN, 34, platform_width() - 2*MARGIN, std::string(fs_basename(a->path())).c_str(), false, COLOR_TEXT);
  draw_text_fit(ui_font::regular, MARGIN, 58, platform_width() - 2*MARGIN, a->path().c_str(), true, COLOR_MUTED);

  uint64_t wasted = a->wasted_space();
  char line[160];
  snprintf(line, sizeof(line), "%zu file%s, %s", a->entries().size(), a->entries().size() == 1 ? "" : "s",
           format_size(a->total_size()).c_str());
  if(a->is_remote()) snprintf(line + strlen(line), sizeof(line) - strlen(line), "  |  remote: Add uploads");
  else if(wasted) snprintf(line + strlen(line), sizeof(line) - strlen(line), "  |  %s unused (Compact)", format_size(wasted).c_str());
  draw_text(ui_font::regular, MARGIN, 80, line, COLOR_MUTED);
  draw_list(app);
}

/**
 * @brief Draws ui_screen::pick_add and ui_screen::pick_extract.
 * @param app Application state.
 */
static void draw_pick_screen(app_state &app){
  const char *title = app.screen == ui_screen::pick_add ? "Choose the file to add"
                    : app.extract_all ? "Choose the folder to extract everything into"
                    : "Choose the folder to extract into";
  draw_text(ui_font::title, MARGIN, 34, title, COLOR_TEXT);
  draw_folder_line(app);
  draw_list(app);
}

void draw(app_state &app){
  fill(0, 0, platform_width(), platform_height(), COLOR_BG);

  if(app.screen == ui_screen::open) draw_open_screen(app);
  else if(app.screen == ui_screen::archive) draw_archive_screen(app);
  else draw_pick_screen(app);

  draw_buttons(app);
  draw_text_fit(ui_font::regular, MARGIN, status_y(), platform_width() - 2*MARGIN, status_text, false,
                status_error ? COLOR_ERROR : COLOR_OK);

  platform_present();
}

}
