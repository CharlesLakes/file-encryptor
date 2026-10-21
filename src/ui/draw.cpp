/**
 * @file    draw.cpp
 * @brief   Painting of each screen into the off-screen buffer.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.hpp"

#include <algorithm>
#include <string>
#include "core/fs.hpp"
#include "core/status.hpp"
#include "core/util.hpp"

namespace crls{

/* ---------------- Primitives ---------------- */

/**
 * @brief Fills a rectangle with a solid color.
 * @param r     Rectangle.
 * @param color Fill color (0xRRGGBB).
 */
static void fill(const rect &r, unsigned long color){
  platform_fill_rect(r.x, r.y, r.w, r.h, color);
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
static void draw_text_fit(ui_font f, int x, int baseline, int max_w, std::string_view s,
                          bool keep_tail, unsigned long color){
  if(s.empty() || platform_text_width(f, s) <= max_w){
    platform_draw_text(f, x, baseline, s, color);
    return;
  }

  /* Keeps the first or last n characters and adds "...". */
  std::string out;
  auto shorten = [&](std::size_t n){
    if(keep_tail) out.assign("...").append(s.substr(s.size() - n));
    else out.assign(s.substr(0, n)).append("...");
    return platform_text_width(f, out) <= max_w;
  };
  /* Dropping characters never makes the text wider, so the longest version
     that fits is found by bisection; with no room at all only "..." is left. */
  std::size_t lo = 0, hi = s.size() - 1;
  while(lo < hi){
    std::size_t mid = (lo + hi + 1) / 2;
    if(shorten(mid)) lo = mid;
    else hi = mid - 1;
  }
  shorten(lo);
  platform_draw_text(f, x, baseline, out, color);
}

/* ---------------- Widgets ---------------- */

/**
 * @brief Draws the list of the current screen with its scroll bar.
 * @param app Application state.
 */
static void draw_list(app_state &app){
  rect area = list_rect(app);
  fill(area, COLOR_PANEL);

  int count = list_count(app), sel = list_sel(app), scroll = list_scroll(app), rows = visible_rows(app);
  int text_y = (ROW_H + platform_font_ascent(ui_font::regular) - platform_font_descent(ui_font::regular)) / 2;

  if(count == 0){
    std::string_view hint = app.screen == ui_screen::archive ? "The .crls is empty. Use \"Add\"." : "(empty folder)";
    platform_draw_text(ui_font::regular, area.x + 12, area.y + text_y, hint, COLOR_MUTED);
  }

  for(int r = 0; r < rows && scroll + r < count; r++){
    int i = scroll + r;
    rect row{area.x, area.y + r*ROW_H, area.w, ROW_H};
    if(i == sel) fill(row, COLOR_SELECTED);
    else if(r % 2) fill(row, COLOR_ROW_ALT);

    int baseline = row.y + text_y;
    if(app.screen == ui_screen::archive){
      const archive_entry &e = app.arc.entries()[static_cast<std::size_t>(i)];
      std::string size_text = format_size(e.size);
      int size_w = platform_text_width(ui_font::regular, size_text);
      draw_text_fit(ui_font::regular, row.x + 12, baseline, row.w - size_w - 48, e.name, false, COLOR_TEXT);
      platform_draw_text(ui_font::regular, row.x + row.w - size_w - 20, baseline, size_text, COLOR_MUTED);
    }else{
      const dir_item &item = app.files.items()[static_cast<std::size_t>(i)];
      std::string label = item.is_dir ? item.name + "/" : item.name;
      unsigned long color = item.is_dir ? COLOR_DIR : (has_crls_ext(item.name) ? COLOR_FILE : COLOR_TEXT);
      draw_text_fit(ui_font::regular, row.x + 12, baseline, row.w - 32, label, false, color);
    }
  }

  if(count > rows){
    int bar_h = std::max(area.h * rows / count, 20);
    int bar_y = area.y + (area.h - bar_h) * scroll / (count - rows);
    fill(rect{area.x + area.w - 6, bar_y, 4, bar_h}, COLOR_BORDER);
  }
}

/**
 * @brief Draws a labeled text field of ui_screen::open.
 * @param app    Application state.
 * @param index  Field.
 * @param label  Label displayed to the left.
 * @param value  Field content.
 * @param secret True to mask the content with asterisks.
 */
static void draw_field(app_state &app, ui_field index, std::string_view label, std::string_view value, bool secret){
  rect r = field_rect(index);
  int text_y = r.y + (r.h + platform_font_ascent(ui_font::regular) - platform_font_descent(ui_font::regular)) / 2;

  platform_draw_text(ui_font::regular, MARGIN, text_y, label, COLOR_TEXT);
  fill(r, COLOR_PANEL);
  platform_draw_rect(r.x, r.y, r.w, r.h, app.focus == index ? COLOR_FOCUS : COLOR_BORDER);

  /* A secret field only shows how many characters it holds. */
  std::string masked;
  if(secret) masked.assign(value.size(), '*');
  std::string_view shown = secret ? std::string_view(masked) : value;
  draw_text_fit(ui_font::regular, r.x + 8, text_y, r.w - 24, shown, true, COLOR_TEXT);

  if(app.focus == index){
    int cursor_x = std::min(r.x + 8 + platform_text_width(ui_font::regular, shown), r.x + r.w - 14);
    fill(rect{cursor_x + 1, r.y + 6, 2, r.h - 12}, COLOR_FOCUS);
  }
}

/**
 * @brief Draws the buttons of the current screen.
 * @param app Application state.
 */
static void draw_buttons(app_state &app){
  button_row row = get_buttons(app);
  for(const button &b : row.list()){
    fill(b.area, b.color);
    unsigned long text_color = b.color == COLOR_BUTTON ? COLOR_TEXT : COLOR_DARK_TEXT;
    int tx = b.area.x + (b.area.w - platform_text_width(ui_font::bold, b.label)) / 2;
    int ty = b.area.y + (b.area.h + platform_font_ascent(ui_font::bold) - platform_font_descent(ui_font::bold)) / 2;
    platform_draw_text(ui_font::bold, tx, ty, b.label, text_color);
  }
}

/* ---------------- Screens ---------------- */

/**
 * @brief Draws the path of the browsed folder below the title.
 * @param app Application state.
 */
static void draw_folder_line(app_state &app){
  draw_text_fit(ui_font::regular, MARGIN, 60, platform_width() - 2*MARGIN, "Folder: " + app.files.dir(), true,
                COLOR_MUTED);
}

/**
 * @brief Draws ui_screen::open.
 * @param app Application state.
 */
static void draw_open_screen(app_state &app){
  platform_draw_text(ui_font::title, MARGIN, 34, ".crls archive manager", COLOR_TEXT);
  draw_folder_line(app);
  draw_list(app);
  draw_field(app, ui_field::path, ".crls file or URL", app.path_field, false);
  draw_field(app, ui_field::password, "Password", app.password_field.view(), true);
  draw_field(app, ui_field::confirm, "Confirm (create)", app.confirm_field.view(), true);
}

/**
 * @brief Draws ui_screen::archive.
 * @param app Application state.
 */
static void draw_archive_screen(app_state &app){
  const archive &a = app.arc;
  int width = platform_width() - 2*MARGIN;
  draw_text_fit(ui_font::title, MARGIN, 34, width, fs_basename(a.path()), false, COLOR_TEXT);
  draw_text_fit(ui_font::regular, MARGIN, 58, width, a.path(), true, COLOR_MUTED);

  std::size_t files = a.entries().size();
  uint64_t wasted = a.wasted_space();
  std::string line = std::to_string(files) + (files == 1 ? " file, " : " files, ") + format_size(a.total_size());
  if(a.is_remote()) line += "  |  remote: Add uploads";
  else if(wasted) line += "  |  " + format_size(wasted) + " unused (Compact)";
  platform_draw_text(ui_font::regular, MARGIN, 80, line, COLOR_MUTED);
  draw_list(app);
}

/**
 * @brief Draws ui_screen::pick_add and ui_screen::pick_extract.
 * @param app Application state.
 */
static void draw_pick_screen(app_state &app){
  std::string_view title = app.screen == ui_screen::pick_add ? "Choose the file to add"
                         : app.extract_all ? "Choose the folder to extract everything into"
                         : "Choose the folder to extract into";
  platform_draw_text(ui_font::title, MARGIN, 34, title, COLOR_TEXT);
  draw_folder_line(app);
  draw_list(app);
}

void draw(app_state &app){
  fill(rect{0, 0, platform_width(), platform_height()}, COLOR_BG);

  if(app.screen == ui_screen::open) draw_open_screen(app);
  else if(app.screen == ui_screen::archive) draw_archive_screen(app);
  else draw_pick_screen(app);

  draw_buttons(app);
  draw_text_fit(ui_font::regular, MARGIN, status_y(), platform_width() - 2*MARGIN, status_message(), false,
                status_is_error() ? COLOR_ERROR : COLOR_OK);

  platform_present();
}

}
