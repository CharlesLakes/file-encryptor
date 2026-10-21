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

/** @brief Win32 resources of the window. */
static struct{
  HWND win;                      /**< Main window. */
  HDC buffer;                    /**< Memory device context of the off-screen buffer. */
  HBITMAP bitmap;                /**< Bitmap selected into buffer. */
  HGDIOBJ old_bitmap;            /**< Bitmap originally selected into buffer. */
  int width, height;             /**< Size of the client area and the buffer. */
  int min_width, min_height;     /**< Minimum size of the client area. */
  void (*redraw)(void);          /**< Repaints the window during a resize. */
  HFONT fonts[FONT_COUNT];       /**< Loaded fonts, indexed by FONT_*. */
  TEXTMETRICA metrics[FONT_COUNT]; /**< Metrics of each font. */
  ui_event pending;              /**< Event waiting to be returned. */
  bool has_pending;              /**< True if pending holds an event. */
}w32;

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
 * @brief  Translates a virtual key code into a KEY_* value.
 * @param  vk Virtual key code.
 * @return KEY_* value, or KEY_NONE if the key is not used.
 */
static int translate_key(WPARAM vk){
  switch(vk){
    case VK_UP:     return KEY_UP;
    case VK_DOWN:   return KEY_DOWN;
    case VK_PRIOR:  return KEY_PAGE_UP;
    case VK_NEXT:   return KEY_PAGE_DOWN;
    case VK_HOME:   return KEY_HOME;
    case VK_END:    return KEY_END;
    case VK_TAB:    return GetKeyState(VK_SHIFT) < 0 ? KEY_BACK_TAB : KEY_TAB;
    case VK_RETURN: return KEY_ENTER;
    case VK_ESCAPE: return KEY_ESCAPE;
    case VK_BACK:   return KEY_BACKSPACE;
    case VK_DELETE: return KEY_DELETE;
  }
  return KEY_NONE;
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
  memset(&event, 0, sizeof(event));

  switch(msg){
    case WM_PAINT:{
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      if(w32.buffer) BitBlt(dc, 0, 0, w32.width, w32.height, w32.buffer, 0, 0, SRCCOPY);
      EndPaint(hwnd, &ps);
      event.type = EVENT_EXPOSE;
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
        event.type = EVENT_RESIZE;
        post_event(&event);
      }
      return 0;
    }
    case WM_LBUTTONDOWN:
      event.type = EVENT_CLICK;
      event.x = (short) LOWORD(lparam);
      event.y = (short) HIWORD(lparam);
      event.time = (unsigned long) GetMessageTime();
      post_event(&event);
      return 0;
    case WM_MOUSEWHEEL:{
      /* Wheel messages carry screen coordinates. */
      POINT p = {(short) LOWORD(lparam), (short) HIWORD(lparam)};
      ScreenToClient(hwnd, &p);
      event.type = EVENT_WHEEL;
      event.x = p.x;
      event.y = p.y;
      event.wheel = GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? -1 : 1;
      post_event(&event);
      return 0;
    }
    case WM_KEYDOWN:
      event.key = translate_key(wparam);
      if(event.key != KEY_NONE){
        event.type = EVENT_KEY;
        post_event(&event);
        return 0;
      }
      break;
    case WM_CHAR:
      if(wparam >= 32 && wparam < 127){
        event.type = EVENT_KEY;
        event.key = KEY_CHAR;
        event.ch = (char) wparam;
        post_event(&event);
      }
      return 0;
    case WM_CLOSE:
      /* The window is destroyed by platform_close(), not here. */
      event.type = EVENT_QUIT;
      post_event(&event);
      return 0;
  }
  return DefWindowProcA(hwnd, msg, wparam, lparam);
}

/**
 * @brief  Creates a monospaced font and reads its metrics.
 * @param  index  FONT_* value to fill.
 * @param  height Character cell height in pixels.
 * @param  weight FW_NORMAL or FW_BOLD.
 * @return True on success, false otherwise.
 */
static bool load_font(int index, int height, int weight){
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
    return false;
  }
  w32.old_bitmap = SelectObject(w32.buffer, w32.bitmap);
  SetBkMode(w32.buffer, TRANSPARENT);
  SetTextAlign(w32.buffer, TA_BASELINE | TA_LEFT);

  if(!load_font(FONT_REGULAR, 16, FW_NORMAL) || !load_font(FONT_BOLD, 16, FW_BOLD) ||
     !load_font(FONT_TITLE, 21, FW_BOLD)){
    platform_error("Could not load a font.");
    return false;
  }

  ShowWindow(w32.win, SW_SHOWNORMAL);
  UpdateWindow(w32.win);
  return true;
}

void platform_set_redraw(void (*redraw)(void)){
  w32.redraw = redraw;
}

int platform_width(void){
  return w32.width;
}

int platform_height(void){
  return w32.height;
}

void platform_close(void){
  if(w32.buffer){
    SelectObject(w32.buffer, w32.old_bitmap);
    DeleteDC(w32.buffer);
  }
  if(w32.bitmap) DeleteObject(w32.bitmap);
  for(int i = 0; i < FONT_COUNT; i++)
    if(w32.fonts[i]) DeleteObject(w32.fonts[i]);
  if(w32.win) DestroyWindow(w32.win);
  memset(&w32, 0, sizeof(w32));
}

void platform_error(const char *message){
  MessageBoxA(NULL, message, "FileEncryptor", MB_OK | MB_ICONERROR);
}

void platform_next_event(ui_event *event){
  while(!w32.has_pending){
    MSG msg;
    if(GetMessageA(&msg, NULL, 0, 0) <= 0){
      memset(event, 0, sizeof(*event));
      event->type = EVENT_QUIT;
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

void platform_draw_text(int font, int x, int baseline, const char *s, int len, unsigned long color){
  SelectObject(w32.buffer, w32.fonts[font]);
  SetTextColor(w32.buffer, to_colorref(color));
  TextOutA(w32.buffer, x, baseline, s, len);
}

int platform_text_width(int font, const char *s, int len){
  SIZE size;
  SelectObject(w32.buffer, w32.fonts[font]);
  return GetTextExtentPoint32A(w32.buffer, s, len, &size) ? size.cx : 0;
}

int platform_font_ascent(int font){
  return w32.metrics[font].tmAscent;
}

int platform_font_descent(int font){
  return w32.metrics[font].tmDescent;
}

void platform_present(void){
  HDC dc = GetDC(w32.win);
  BitBlt(dc, 0, 0, w32.width, w32.height, w32.buffer, 0, 0, SRCCOPY);
  ReleaseDC(w32.win, dc);
}

}
