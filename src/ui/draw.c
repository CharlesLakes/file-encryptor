/**
 * @file    draw.c
 * @brief   Painting of each screen into the off-screen buffer.
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 */

#include "ui/ui.h"

#include <stdio.h>
#include <string.h>
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
  XSetForeground(gui.dpy, gui.gc, color);
  XFillRectangle(gui.dpy, gui.buffer, gui.gc, x, y, (unsigned int) w, (unsigned int) h);
}

/**
 * @brief Draws a string.
 * @param f        Font.
 * @param x        Left coordinate.
 * @param baseline Baseline coordinate.
 * @param s        String.
 * @param color    Text color (0xRRGGBB).
 */
static void draw_text(XFontStruct *f, int x, int baseline, const char *s, unsigned long color){
  XSetForeground(gui.dpy, gui.gc, color);
  XSetFont(gui.dpy, gui.gc, f->fid);
  XDrawString(gui.dpy, gui.buffer, gui.gc, x, baseline, s, (int) strlen(s));
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
static void draw_text_fit(XFontStruct *f, int x, int baseline, int max_w, const char *s,
                          bool keep_tail, unsigned long color){
  int len = (int) strlen(s);
  if(XTextWidth(f, s, len) <= max_w){
    draw_text(f, x, baseline, s, color);
    return;
  }

  char out[PATH_MAX + 4];
  int keep = len;
  while(keep > 0){
    keep--;
    if(keep_tail) snprintf(out, sizeof(out), "...%s", s + len - keep);
    else snprintf(out, sizeof(out), "%.*s...", keep, s);
    if(XTextWidth(f, out, (int) strlen(out)) <= max_w) break;
  }
  draw_text(f, x, baseline, out, color);
}

/**
 * @brief  Measures the width of a string.
 * @param  f Font.
 * @param  s String.
 * @return Width in pixels.
 */
static int text_width(XFontStruct *f, const char *s){
  return XTextWidth(f, s, (int) strlen(s));
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
  int text_y = (ROW_H + gui.font->ascent - gui.font->descent) / 2;

  if(count == 0){
    const char *hint = app.screen == SCREEN_ARCHIVE ? "The .crls is empty. Use \"Add\"."
                                                    : "(empty folder)";
    draw_text(gui.font, x + 12, y + text_y, hint, COLOR_MUTED);
  }

  for(int r = 0; r < rows && scroll + r < count; r++){
    int i = scroll + r, row_y = y + r*ROW_H;
    if(i == sel) fill(x, row_y, w, ROW_H, COLOR_SELECTED);
    else if(r % 2) fill(x, row_y, w, ROW_H, COLOR_ROW_ALT);

    if(app.screen == SCREEN_ARCHIVE){
      archive_entry *e = archive_get_entry(&app.archive, (unsigned int) i);
      char size_text[32];
      format_size(e->size, size_text, sizeof(size_text));
      int size_w = text_width(gui.font, size_text);
      draw_text_fit(gui.font, x + 12, row_y + text_y, w - size_w - 48, e->name, false, COLOR_TEXT);
      draw_text(gui.font, x + w - size_w - 20, row_y + text_y, size_text, COLOR_MUTED);
    }else{
      dir_item *item = browser_get_item(&app.browser, (unsigned int) i);
      char label[MAX_NAME + 2];
      snprintf(label, sizeof(label), item->is_dir ? "%s/" : "%s", item->name);
      unsigned long color = item->is_dir ? COLOR_DIR : (has_crls_ext(item->name) ? COLOR_FILE : COLOR_TEXT);
      draw_text_fit(gui.font, x + 12, row_y + text_y, w - 32, label, false, color);
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
  int text_y = y + (h + gui.font->ascent - gui.font->descent) / 2;

  draw_text(gui.font, MARGIN, text_y, label, COLOR_TEXT);
  fill(x, y, w, h, COLOR_PANEL);
  XSetForeground(gui.dpy, gui.gc, app.focus == index ? COLOR_FOCUS : COLOR_BORDER);
  XDrawRectangle(gui.dpy, gui.buffer, gui.gc, x, y, (unsigned int) w - 1, (unsigned int) h - 1);

  char shown[PATH_MAX];
  if(secret){
    size_t len = strlen(value);
    memset(shown, '*', len);
    shown[len] = '\0';
  }else{
    snprintf(shown, sizeof(shown), "%s", value);
  }
  draw_text_fit(gui.font, x + 8, text_y, w - 24, shown, true, COLOR_TEXT);

  if(app.focus == index){
    int len = (int) strlen(shown);
    int cursor_x = x + 8 + XTextWidth(gui.font, shown, len);
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
    int tx = b[i].x + (b[i].w - text_width(gui.bold_font, b[i].label)) / 2;
    int ty = b[i].y + (b[i].h + gui.bold_font->ascent - gui.bold_font->descent) / 2;
    draw_text(gui.bold_font, tx, ty, b[i].label, text_color);
  }
}

/* ---------------- Screens ---------------- */

/**
 * @brief Draws the path of the browsed folder below the title.
 */
static void draw_folder_line(void){
  char line[PATH_MAX + 16];
  snprintf(line, sizeof(line), "Folder: %s", app.browser.dir);
  draw_text_fit(gui.font, MARGIN, 60, WIDTH - 2*MARGIN, line, true, COLOR_MUTED);
}

/**
 * @brief Draws SCREEN_OPEN.
 */
static void draw_open_screen(void){
  draw_text(gui.title_font, MARGIN, 34, ".crls archive manager", COLOR_TEXT);
  draw_folder_line();
  draw_list();
  draw_field(FIELD_PATH, ".crls file", app.path_field, false);
  draw_field(FIELD_PASSWORD, "Password", app.password_field, true);
  draw_field(FIELD_CONFIRM, "Confirm (create)", app.confirm_field, true);
}

/**
 * @brief Draws SCREEN_ARCHIVE.
 */
static void draw_archive_screen(void){
  archive *a = &app.archive;
  const char *name = strrchr(a->path, '/');
  draw_text_fit(gui.title_font, MARGIN, 34, WIDTH - 2*MARGIN, name ? name + 1 : a->path, false, COLOR_TEXT);
  draw_text_fit(gui.font, MARGIN, 58, WIDTH - 2*MARGIN, a->path, true, COLOR_MUTED);

  uint64_t wasted = archive_wasted_space(a);
  char size_text[32], wasted_text[32], line[160];
  format_size(archive_total_size(a), size_text, sizeof(size_text));
  format_size(wasted, wasted_text, sizeof(wasted_text));
  snprintf(line, sizeof(line), "%u file%s, %s", a->entries.n, a->entries.n == 1 ? "" : "s", size_text);
  if(wasted) snprintf(line + strlen(line), sizeof(line) - strlen(line), "  |  %s unused (Compact)", wasted_text);
  draw_text(gui.font, MARGIN, 80, line, COLOR_MUTED);
  draw_list();
}

/**
 * @brief Draws SCREEN_PICK_ADD and SCREEN_PICK_EXTRACT.
 */
static void draw_pick_screen(void){
  const char *title = app.screen == SCREEN_PICK_ADD ? "Choose the file to add"
                    : app.extract_all ? "Choose the folder to extract everything into"
                    : "Choose the folder to extract into";
  draw_text(gui.title_font, MARGIN, 34, title, COLOR_TEXT);
  draw_folder_line();
  draw_list();
}

void draw(void){
  fill(0, 0, WIDTH, HEIGHT, COLOR_BG);

  if(app.screen == SCREEN_OPEN) draw_open_screen();
  else if(app.screen == SCREEN_ARCHIVE) draw_archive_screen();
  else draw_pick_screen();

  draw_buttons();
  draw_text_fit(gui.font, MARGIN, STATUS_Y, WIDTH - 2*MARGIN, status_text, false,
                status_error ? COLOR_ERROR : COLOR_OK);

  XCopyArea(gui.dpy, gui.buffer, gui.win, gui.gc, 0, 0, WIDTH, HEIGHT, 0, 0);
  XFlush(gui.dpy);
}
