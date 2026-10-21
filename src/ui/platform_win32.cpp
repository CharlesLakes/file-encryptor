/**
 * @file    platform_win32.cpp
 * @brief   Win32 and GDI implementation of the windowing system abstraction
 *          (see platform.h).
 * @author  @CharlesLakes
 *
 * Maintainer: @CharlesLakes
 *
 * @section platform_win32_events Event Delivery
 *
 * Win32 delivers input to a window procedure instead of returning it to the
 * caller. window_proc() stores the translated event in a single pending slot
 * and platform_next_event() pumps messages until that slot is filled.
 *
 * Navigation keys are read from WM_KEYDOWN and printable characters from
 * WM_CHAR, so each key press produces exactly one event.
 *
 * While the user drags a border, Win32 runs its own message loop and
 * platform_next_event() does not return, so WM_SIZE repaints through the
 * function registered with platform_set_redraw().
 */

#include "ui/platform.h"

#include <string.h>
#include <windows.h>

namespace crls{

/**
 * @brief Win32 resources of the window. Everything created is released by
 *        release(), also when platform_open() fails halfway.
 */
struct win32_state{
  HWND win = NULL;                           /**< Main window. */
  HDC buffer = NULL;                         /**< Memory device context of the off-screen buffer. */
  HBITMAP bitmap = NULL;                     /**< Bitmap selected into buffer. */
  HGDIOBJ old_bitmap = NULL;                 /**< Bitmap originally selected into buffer. */
  int width = 0, height = 0;                 /**< Size of the client area and the buffer. */
  int min_width = 0, min_height = 0;         /**< Minimum size of the client area. */
  function_ref<void()> redraw;               /**< Repaints the window during a resize. */
  HFONT fonts[UI_FONT_COUNT] = {};           /**< Loaded fonts, indexed by ui_font. */
  TEXTMETRICA metrics[UI_FONT_COUNT] = {};   /**< Metrics of each font. */
  ui_event pending;                          /**< Event waiting to be returned. */
  bool has_pending = false;                  /**< True if pending holds an event. */

  ~win32_state(){ release(); }

  /** @brief Frees every resource that exists. */
  void release() noexcept{
    if(buffer){
      SelectObject(buffer, old_bitmap);
      DeleteDC(buffer);
    }
    if(bitmap) DeleteObject(bitmap);
    for(HFONT f : fonts)
      if(f) DeleteObject(f);
    if(win) DestroyWindow(win);
    *this = win32_state();
  }

  /** @brief The font for a ui_font value. */
  HFONT font(ui_font f) const noexcept{ return fonts[static_cast<int>(f)]; }

  /** @brief The metrics of a ui_font value. */
  const TEXTMETRICA& metric(ui_font f) const noexcept{ return metrics[static_cast<int>(f)]; }

  win32_state() = default;
  win32_state& operator=(const win32_state &) = default;
};

static win32_state w32;

/** @brief Name of the registered window class. */
static const char CLASS_NAME[] = "FileEncryptorWindow";

/**
 * @brief  Converts a 0xRRGGBB color into a GDI COLORREF (0x00BBGGRR).
 * @param  color Color.
 * @return Equivalent COLORREF.
 */
static COLORREF to_colorref(unsigned long color){
  return RGB((color >> 16) & 0xff, (color >> 8) & 0xff, color & 0xff);
}

/**
 * @brief Stores an event for platform_next_event(), replacing any older one.
 * @param event Event.
 */
static void post_event(const ui_event *event){
  w32.pending = *event;
  w32.has_pending = true;
}

/**
 * @brief  Replaces the off-screen buffer with one of the given size, keeping
 *         the fonts and drawing modes of the device context.
 * @param  width  New width.
 * @param  height New height.
 * @return True on success, false otherwise (the old buffer is kept).
 */
static bool resize_buffer(int width, int height){
  HDC dc = GetDC(w32.win);
  HBITMAP bitmap = CreateCompatibleBitmap(dc, width, height);
  ReleaseDC(w32.win, dc);
  if(!bitmap) return false;
  SelectObject(w32.buffer, bitmap);
  DeleteObject(w32.bitmap);
  w32.bitmap = bitmap;
  w32.width = width;
  w32.height = height;
  return true;
}

/**
 * @brief Grows the outer size of the window so that its client area is
 *        width x height pixels.
 * @param style  Window style.
 * @param width  Client area width.
 * @param height Client area height.
 * @param out_w  Output: outer width.
 * @param out_h  Output: outer height.
 */
static void outer_size(DWORD style, int width, int height, int *out_w, int *out_h){
  RECT r = {0, 0, width, height};
  AdjustWindowRect(&r, style, FALSE);
  *out_w = r.right - r.left;
  *out_h = r.bottom - r.top;
}

/**
 * @brief  Translates a virtual key code into a ui_key value.
 * @param  vk Virtual key code.
 * @return The key, or ui_key::none if the key is not used.
 */
static ui_key translate_key(WPARAM vk){
  switch(vk){
    case VK_UP:     return ui_key::up;
    case VK_DOWN:   return ui_key::down;
    case VK_PRIOR:  return ui_key::page_up;
    case VK_NEXT:   return ui_key::page_down;
    case VK_HOME:   return ui_key::home;
    case VK_END:    return ui_key::end;
    case VK_TAB:    return GetKeyState(VK_SHIFT) < 0 ? ui_key::back_tab : ui_key::tab;
    case VK_RETURN: return ui_key::enter;
    case VK_ESCAPE: return ui_key::escape;
    case VK_BACK:   return ui_key::backspace;
    case VK_DELETE: return ui_key::del;
  }
  return ui_key::none;
}

/**
 * @brief  Window procedure: translates Win32 messages into ui_event values.
 * @param  hwnd   Window.
 * @param  msg    Message identifier.
 * @param  wparam Message parameter.
 * @param  lparam Message parameter.
 * @return Message result.
 */
static LRESULT CALLBACK window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam){
  ui_event event;

  switch(msg){
    case WM_PAINT:{
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      if(w32.buffer) BitBlt(dc, 0, 0, w32.width, w32.height, w32.buffer, 0, 0, SRCCOPY);
      EndPaint(hwnd, &ps);
      event.type = ui_event_type::expose;
      post_event(&event);
      return 0;
    }
    case WM_ERASEBKGND:
      /* The whole client area is covered by the buffer. */
      return 1;
    case WM_GETMINMAXINFO:{
      MINMAXINFO *info = (MINMAXINFO *) lparam;
      int w, h;
      outer_size((DWORD) GetWindowLongA(hwnd, GWL_STYLE), w32.min_width, w32.min_height, &w, &h);
      info->ptMinTrackSize.x = w;
      info->ptMinTrackSize.y = h;
      return 0;
    }
    case WM_SIZE:{
      int w = LOWORD(lparam), h = HIWORD(lparam);
      /* A minimized window reports 0 x 0: keep the current buffer. */
      if(!w32.buffer || w == 0 || h == 0 || (w == w32.width && h == w32.height)) return 0;
      if(resize_buffer(w, h)){
        if(w32.redraw) w32.redraw();
        event.type = ui_event_type::resize;
        post_event(&event);
      }
      return 0;
    }
    case WM_LBUTTONDOWN:
      event.type = ui_event_type::click;
      event.x = (short) LOWORD(lparam);
      event.y = (short) HIWORD(lparam);
      event.time = (unsigned long) GetMessageTime();
      post_event(&event);
      return 0;
    case WM_MOUSEWHEEL:{
      /* Wheel messages carry screen coordinates. */
      POINT p = {(short) LOWORD(lparam), (short) HIWORD(lparam)};
      ScreenToClient(hwnd, &p);
      event.type = ui_event_type::wheel;
      event.x = p.x;
      event.y = p.y;
      event.wheel = GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -1 : 1;
      post_event(&event);
      return 0;
    }
    case WM_KEYDOWN:
      event.key = translate_key(wparam);
      if(event.key != ui_key::none){
        event.type = ui_event_type::key;
        post_event(&event);
        return 0;
      }
      break;
    case WM_CHAR:
      if(wparam >= 32 && wparam < 127){
        event.type = ui_event_type::key;
        event.key = ui_key::character;
        event.ch = (char) wparam;
        post_event(&event);
      }
      return 0;
    case WM_CLOSE:
      /* The window is destroyed by platform_close(), not here. */
      event.type = ui_event_type::quit;
      post_event(&event);
      return 0;
  }
  return DefWindowProcA(hwnd, msg, wparam, lparam);
}

/**
 * @brief  Creates a monospaced font and reads its metrics.
 * @param  font   Font to fill.
 * @param  height Character cell height in pixels.
 * @param  weight FW_NORMAL or FW_BOLD.
 * @return True on success, false otherwise.
 */
static bool load_font(ui_font font, int height, int weight){
  int index = static_cast<int>(font);
  w32.fonts[index] = CreateFontA(height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                 FIXED_PITCH | FF_MODERN, "Consolas");
  if(!w32.fonts[index]) return false;
  SelectObject(w32.buffer, w32.fonts[index]);
  return GetTextMetricsA(w32.buffer, &w32.metrics[index]) != 0;
}

bool platform_open(int width, int height, const char *title, unsigned long background){
  HINSTANCE instance = GetModuleHandleA(NULL);
  WNDCLASSA wc;
  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.hCursor = LoadCursorA(NULL, (LPCSTR) IDC_ARROW);
  wc.hbrBackground = CreateSolidBrush(to_colorref(background));
  wc.lpszClassName = CLASS_NAME;
  if(!RegisterClassA(&wc)){
    platform_error("Could not register the window class.");
    return false;
  }

  DWORD style = WS_OVERLAPPEDWINDOW;
  int outer_w, outer_h;
  outer_size(style, width, height, &outer_w, &outer_h);
  w32.min_width = width;
  w32.min_height = height;
  w32.win = CreateWindowExA(0, CLASS_NAME, title, style, CW_USEDEFAULT, CW_USEDEFAULT,
                            outer_w, outer_h, NULL, NULL, instance, NULL);
  if(!w32.win){
    platform_error("Could not create the window.");
    return false;
  }

  HDC dc = GetDC(w32.win);
  w32.width = width;
  w32.height = height;
  w32.buffer = CreateCompatibleDC(dc);
  w32.bitmap = CreateCompatibleBitmap(dc, width, height);
  ReleaseDC(w32.win, dc);
  if(!w32.buffer || !w32.bitmap){
    platform_error("Could not create the drawing buffer.");
    w32.release();
    return false;
  }
  w32.old_bitmap = SelectObject(w32.buffer, w32.bitmap);
  SetBkMode(w32.buffer, TRANSPARENT);
  SetTextAlign(w32.buffer, TA_BASELINE | TA_LEFT);

  if(!load_font(ui_font::regular, 16, FW_NORMAL) || !load_font(ui_font::bold, 16, FW_BOLD) ||
     !load_font(ui_font::title, 21, FW_BOLD)){
    platform_error("Could not load a font.");
    w32.release();
    return false;
  }

  ShowWindow(w32.win, SW_SHOWNORMAL);
  UpdateWindow(w32.win);
  return true;
}

void platform_set_redraw(function_ref<void()> redraw){
  w32.redraw = redraw;
}

int platform_width(){
  return w32.width;
}

int platform_height(){
  return w32.height;
}

void platform_close(){
  w32.release();
}

void platform_error(const char *message){
  MessageBoxA(NULL, message, "FileEncryptor", MB_OK | MB_ICONERROR);
}

void platform_next_event(ui_event *event){
  while(!w32.has_pending){
    MSG msg;
    if(GetMessageA(&msg, NULL, 0, 0) <= 0){
      *event = ui_event();
      event->type = ui_event_type::quit;
      return;
    }
    TranslateMessage(&msg);
    DispatchMessageA(&msg);
  }
  *event = w32.pending;
  w32.has_pending = false;
}

void platform_fill_rect(int x, int y, int w, int h, unsigned long color){
  RECT r = {x, y, x + w, y + h};
  HBRUSH brush = CreateSolidBrush(to_colorref(color));
  FillRect(w32.buffer, &r, brush);
  DeleteObject(brush);
}

void platform_draw_rect(int x, int y, int w, int h, unsigned long color){
  RECT r = {x, y, x + w, y + h};
  HBRUSH brush = CreateSolidBrush(to_colorref(color));
  FrameRect(w32.buffer, &r, brush);
  DeleteObject(brush);
}

void platform_draw_text(ui_font font, int x, int baseline, const char *s, int len, unsigned long color){
  SelectObject(w32.buffer, w32.font(font));
  SetTextColor(w32.buffer, to_colorref(color));
  TextOutA(w32.buffer, x, baseline, s, len);
}

int platform_text_width(ui_font font, const char *s, int len){
  SIZE size;
  SelectObject(w32.buffer, w32.font(font));
  return GetTextExtentPoint32A(w32.buffer, s, len, &size) ? size.cx : 0;
}

int platform_font_ascent(ui_font font){
  return w32.metric(font).tmAscent;
}

int platform_font_descent(ui_font font){
  return w32.metric(font).tmDescent;
}

void platform_present(){
  HDC dc = GetDC(w32.win);
  BitBlt(dc, 0, 0, w32.width, w32.height, w32.buffer, 0, 0, SRCCOPY);
  ReleaseDC(w32.win, dc);
}

}
