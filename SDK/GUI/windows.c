#include <windows.h>

#include <undoscrt/heap.h>
#include <userspace.h>

typedef struct DCState {
  uint32_t handle;
  COLORREF textColor;
  COLORREF backgroundColor;
  uint32_t opaque;
  struct DCState *next;
} DCState;

static DWORD lastError;
static BOOL quitPending;
static int quitCode;
static DCState *dcStates;

static uint32_t window_handle(HWND window) {
  return (uint32_t)(uintptr_t)window;
}

static uint32_t dc_handle(HDC dc) {
  return (uint32_t)(uintptr_t)dc;
}

static BOOL syscall_result(uint32_t result) {
  if (result == SyscallReturnCode_OKAY) return TRUE;
  lastError = result;
  return FALSE;
}

static void copy_message(MSG *destination, const struct Msg *source) {
  destination->hwnd = (HWND)(uintptr_t)source->hwnd;
  destination->message = source->message;
  destination->wParam = source->wParam;
  destination->lParam = (LPARAM)(intptr_t)source->lParam;
  destination->time = source->time;
  destination->pt.x = 0;
  destination->pt.y = 0;
}

static DCState *find_dc(uint32_t handle) {
  for (DCState *state = dcStates; state; state = state->next)
    if (state->handle == handle) return state;
  return NULL;
}

static BOOL set_dc_colors(DCState *state) {
  return syscall_result(UD_SYS_SetDrawColorsV1(state->handle, state->textColor,
                                                state->backgroundColor, state->opaque));
}

DWORD GetLastError(void) {
  return lastError;
}

void SetLastError(DWORD error) {
  lastError = error;
}

ATOM RegisterClassA(const WNDCLASSA *windowClass) {
  if (!windowClass || !windowClass->lpfnWndProc || !windowClass->lpszClassName) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return 0;
  }

  const uintptr_t procedure = (uintptr_t)windowClass->lpfnWndProc;
  const uintptr_t brush = (uintptr_t)windowClass->hbrBackground;
  if (windowClass->lpszMenuName || brush > 16) {
    lastError = SyscallReturnCode_UNSUPPORTED;
    return 0;
  }
  const uint32_t background = brush ? (uint32_t)(brush - 1) : 7;
  if (!syscall_result(UD_SYS_RegisterClassV1(windowClass->lpszClassName,
                                              (uint32_t)procedure, background)))
    return 0;
  return 1;
}

HWND CreateWindowExA(DWORD exStyle, LPCSTR className, LPCSTR windowName, DWORD style,
                     int x, int y, int width, int height, HWND parent, HMENU menu,
                     HINSTANCE instance, LPVOID parameter) {
  (void)instance;
  if (!className || !windowName || exStyle || (style & WS_POPUP)) {
    lastError = exStyle || (style & WS_POPUP)
                    ? SyscallReturnCode_UNSUPPORTED
                    : SyscallReturnCode_INVALID_ARGUMENT;
    return NULL;
  }
  const DWORD supportedStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | WS_CAPTION |
                               WS_OVERLAPPEDWINDOW;
  if (style & ~supportedStyle) {
    lastError = SyscallReturnCode_UNSUPPORTED;
    return NULL;
  }
  if (x == CW_USEDEFAULT) x = 0;
  if (y == CW_USEDEFAULT) y = 0;
  if (width == CW_USEDEFAULT) width = 320;
  if (height == CW_USEDEFAULT) height = 200;

  uint32_t nativeStyle = 0;
  if (style & WS_CAPTION) nativeStyle |= WindowStyle_Caption | WindowStyle_Border;
  else if (style & WS_BORDER) nativeStyle |= WindowStyle_Border;
  if (style & WS_CHILD) nativeStyle |= WindowStyle_Child;

  uint32_t handle = 0;
  if (!syscall_result(UD_SYS_CreateWindowV1(className, windowName, nativeStyle, x, y,
                                              width, height, window_handle(parent),
                                              (uint32_t)(uintptr_t)parameter, &handle)))
    return NULL;
  HWND created = (HWND)(uintptr_t)handle;
  if (menu && !SetMenu(created, menu)) {
    (void)UD_SYS_DestroyWindowV1(handle);
    return NULL;
  }
  if ((style & WS_VISIBLE) &&
      !syscall_result(UD_SYS_ShowWindowV1(handle, ShowCommand_Show))) {
    (void)UD_SYS_DestroyWindowV1(handle);
    return NULL;
  }
  return created;
}

HWND CreateWindowA(LPCSTR className, LPCSTR windowName, DWORD style, int x, int y,
                   int width, int height, HWND parent, HMENU menu, HINSTANCE instance,
                   LPVOID parameter) {
  return CreateWindowExA(0, className, windowName, style, x, y, width, height,
                         parent, menu, instance, parameter);
}

LONG_PTR GetWindowLongPtrA(HWND window, int index) {
  uint32_t value = 0;
  if (!syscall_result(UD_SYS_GetWindowLongPtrV1(window_handle(window),
                                                  (uint32_t)index, &value)))
    return 0;
  return (LONG_PTR)(uintptr_t)value;
}

LONG_PTR SetWindowLongPtrA(HWND window, int index, LONG_PTR value) {
  uint32_t previous = 0;
  if (!syscall_result(UD_SYS_SetWindowLongPtrV1(window_handle(window),
                                                  (uint32_t)index,
                                                  (uint32_t)(uintptr_t)value,
                                                  &previous)))
    return 0;
  return (LONG_PTR)(uintptr_t)previous;
}

LRESULT DispatchMessageA(const MSG *message) {
  if (!message || message->message == WM_QUIT || !message->hwnd) return 0;
  const LONG_PTR address = GetWindowLongPtrA(message->hwnd, GWLP_WNDPROC);
  if (!address) return 0;
  WNDPROC procedure = (WNDPROC)(uintptr_t)address;
  return procedure(message->hwnd, message->message, message->wParam, message->lParam);
}

BOOL DestroyWindow(HWND window) {
  if (!window) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return FALSE;
  }
  MSG destroyed = {0};
  destroyed.hwnd = window;
  destroyed.message = WM_DESTROY;
  (void)DispatchMessageA(&destroyed);
  return syscall_result(UD_SYS_DestroyWindowV1(window_handle(window)));
}

BOOL ShowWindow(HWND window, int command) {
  return syscall_result(UD_SYS_ShowWindowV1(window_handle(window), (uint32_t)command));
}

BOOL MoveWindow(HWND window, int x, int y, int width, int height, BOOL repaint) {
  if (!syscall_result(UD_SYS_MoveWindowV1(window_handle(window), x, y, width, height)))
    return FALSE;
  return !repaint || InvalidateRect(window, NULL, TRUE);
}

BOOL InvalidateRect(HWND window, const RECT *rect, BOOL erase) {
  (void)erase;
  struct Rect nativeRect = {0};
  if (rect) {
    nativeRect.left = rect->left;
    nativeRect.top = rect->top;
    nativeRect.right = rect->right;
    nativeRect.bottom = rect->bottom;
  }
  return syscall_result(UD_SYS_InvalidateRectV1(window_handle(window), &nativeRect,
                                                  rect ? 0 : 1));
}

BOOL GetClientRect(HWND window, RECT *rect) {
  if (!rect) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return FALSE;
  }
  struct Rect nativeRect = {0};
  if (!syscall_result(UD_SYS_GetClientRectV1(window_handle(window), &nativeRect)))
    return FALSE;
  rect->left = nativeRect.left;
  rect->top = nativeRect.top;
  rect->right = nativeRect.right;
  rect->bottom = nativeRect.bottom;
  return TRUE;
}

BOOL GetWindowRect(HWND window, RECT *rect) {
  if (!rect) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return FALSE;
  }
  struct Rect nativeRect = {0};
  if (!syscall_result(UD_SYS_GetWindowRectV1(window_handle(window), &nativeRect)))
    return FALSE;
  rect->left = nativeRect.left;
  rect->top = nativeRect.top;
  rect->right = nativeRect.right;
  rect->bottom = nativeRect.bottom;
  return TRUE;
}

BOOL SetWindowTextA(HWND window, LPCSTR text) {
  if (!text) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return FALSE;
  }
  return syscall_result(UD_SYS_SetWindowTextV1(window_handle(window), text));
}

HMENU CreateMenu(void) {
  uint32_t handle = 0;
  if (!syscall_result(UD_SYS_CreateMenuV1(0, &handle))) return NULL;
  return (HMENU)(uintptr_t)handle;
}

HMENU CreatePopupMenu(void) {
  uint32_t handle = 0;
  if (!syscall_result(UD_SYS_CreateMenuV1(1, &handle))) return NULL;
  return (HMENU)(uintptr_t)handle;
}

BOOL DestroyMenu(HMENU menu) {
  return syscall_result(UD_SYS_DestroyMenuV1((uint32_t)(uintptr_t)menu));
}

BOOL AppendMenuA(HMENU menu, UINT flags, UINT_PTR item, LPCSTR text) {
  const char *label = text ? text : "";
  return syscall_result(UD_SYS_AppendMenuV1((uint32_t)(uintptr_t)menu,
                                              (enum MenuItemFlags)flags,
                                              (uint32_t)item, label));
}

BOOL SetMenu(HWND window, HMENU menu) {
  return syscall_result(UD_SYS_SetMenuV1(window_handle(window),
                                           (uint32_t)(uintptr_t)menu));
}

HMENU GetMenu(HWND window) {
  uint32_t handle = 0;
  if (!syscall_result(UD_SYS_GetMenuV1(window_handle(window), &handle))) return NULL;
  return (HMENU)(uintptr_t)handle;
}

BOOL PostMessageA(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  return syscall_result(UD_SYS_PostMessageV1(window_handle(window), message,
                                               (uint32_t)wParam, (uint32_t)lParam));
}

void PostQuitMessage(int exitCode) {
  quitCode = exitCode;
  quitPending = TRUE;
}

int GetMessageA(MSG *message, HWND window, UINT minimum, UINT maximum) {
  if (!message) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return -1;
  }
  if (window || minimum || maximum) {
    lastError = SyscallReturnCode_UNSUPPORTED;
    return -1;
  }
  struct Msg nativeMessage = {0};
  if (quitPending) {
    const uint32_t result = UD_SYS_PeekMessageV1(&nativeMessage, 1);
    if (result != SyscallReturnCode_TIMEOUT && !syscall_result(result)) return -1;
    if (result == SyscallReturnCode_TIMEOUT) {
      message->hwnd = NULL;
      message->message = WM_QUIT;
      message->wParam = (WPARAM)quitCode;
      message->lParam = 0;
      message->time = 0;
      message->pt.x = 0;
      message->pt.y = 0;
      quitPending = FALSE;
      return 0;
    }
    copy_message(message, &nativeMessage);
    if (message->message == WM_QUIT) quitPending = FALSE;
    return message->message == WM_QUIT ? 0 : 1;
  }
  if (!syscall_result(UD_SYS_GetMessageV1(&nativeMessage))) return -1;
  copy_message(message, &nativeMessage);
  if (message->message == WM_QUIT) quitPending = FALSE;
  return message->message == WM_QUIT ? 0 : 1;
}

BOOL PeekMessageA(MSG *message, HWND window, UINT minimum, UINT maximum, UINT remove) {
  if (!message) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return FALSE;
  }
  if (window || minimum || maximum) {
    lastError = SyscallReturnCode_UNSUPPORTED;
    return FALSE;
  }
  struct Msg nativeMessage = {0};
  const uint32_t result = UD_SYS_PeekMessageV1(&nativeMessage, remove != 0);
  if (result == SyscallReturnCode_TIMEOUT) {
    if (!quitPending) return FALSE;
    message->hwnd = NULL;
    message->message = WM_QUIT;
    message->wParam = (WPARAM)quitCode;
    message->lParam = 0;
    message->time = 0;
    message->pt.x = 0;
    message->pt.y = 0;
    if (remove) quitPending = FALSE;
    return TRUE;
  }
  if (!syscall_result(result)) return FALSE;
  copy_message(message, &nativeMessage);
  if (remove && message->message == WM_QUIT) quitPending = FALSE;
  return TRUE;
}

LRESULT DefWindowProcA(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  (void)wParam;
  (void)lParam;
  if (message == WM_CLOSE) {
    (void)DestroyWindow(window);
  } else if (message == WM_DESTROY) {
    PostQuitMessage(0);
  }
  return 0;
}

BOOL TranslateMessage(const MSG *message) {
  (void)message;
  return FALSE;
}

HDC BeginPaint(HWND window, PAINTSTRUCT *paint) {
  if (!paint) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return NULL;
  }
  uint32_t handle = 0;
  struct PaintInfo info = {0};
  if (!syscall_result(UD_SYS_BeginPaintV1(window_handle(window), &handle, &info)))
    return NULL;
  DCState *state = (DCState *)crt_malloc(sizeof(*state));
  if (!state) {
    lastError = SyscallReturnCode_OUT_OF_MEMORY;
    (void)UD_SYS_EndPaintV1(window_handle(window), handle);
    return NULL;
  }
  state->handle = handle;
  state->textColor = 0;
  state->backgroundColor = 7;
  state->opaque = 1;
  state->next = dcStates;
  dcStates = state;

  paint->hdc = (HDC)(uintptr_t)handle;
  paint->fErase = info.erased != 0;
  paint->rcPaint.left = info.rcPaint.left;
  paint->rcPaint.top = info.rcPaint.top;
  paint->rcPaint.right = info.rcPaint.right;
  paint->rcPaint.bottom = info.rcPaint.bottom;
  paint->fRestore = FALSE;
  paint->fIncUpdate = FALSE;
  for (size_t i = 0; i < sizeof(paint->rgbReserved); ++i) paint->rgbReserved[i] = 0;
  return paint->hdc;
}

BOOL EndPaint(HWND window, const PAINTSTRUCT *paint) {
  if (!paint || !paint->hdc) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return FALSE;
  }
  const uint32_t handle = dc_handle(paint->hdc);
  DCState **link = &dcStates;
  while (*link && (*link)->handle != handle) link = &(*link)->next;
  if (!*link) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return FALSE;
  }
  if (!syscall_result(UD_SYS_EndPaintV1(window_handle(window), handle))) return FALSE;
  DCState *state = *link;
  *link = state->next;
  crt_free(state);
  return TRUE;
}

BOOL Rectangle(HDC dc, int left, int top, int right, int bottom) {
  DCState *state = find_dc(dc_handle(dc));
  if (!state) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return FALSE;
  }
  return set_dc_colors(state) &&
         syscall_result(UD_SYS_RectangleV1(state->handle, left, top, right, bottom));
}

BOOL Ellipse(HDC dc, int left, int top, int right, int bottom) {
  DCState *state = find_dc(dc_handle(dc));
  if (!state) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return FALSE;
  }
  return set_dc_colors(state) &&
         syscall_result(UD_SYS_EllipseV1(state->handle, left, top, right, bottom));
}

BOOL MoveToEx(HDC dc, int x, int y, POINT *previous) {
  if (previous) {
    lastError = SyscallReturnCode_UNSUPPORTED;
    return FALSE;
  }
  DCState *state = find_dc(dc_handle(dc));
  if (!state) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return FALSE;
  }
  return syscall_result(UD_SYS_MoveToV1(state->handle, x, y));
}

BOOL LineTo(HDC dc, int x, int y) {
  DCState *state = find_dc(dc_handle(dc));
  if (!state) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return FALSE;
  }
  return set_dc_colors(state) &&
         syscall_result(UD_SYS_LineToV1(state->handle, x, y));
}

BOOL TextOutA(HDC dc, int x, int y, LPCSTR text, int length) {
  DCState *state = find_dc(dc_handle(dc));
  if (!state || !text || length < 0) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return FALSE;
  }
  for (int i = 0; i < length; ++i) {
    if (!text[i]) {
      lastError = SyscallReturnCode_UNSUPPORTED;
      return FALSE;
    }
  }
  char *terminated = (char *)crt_malloc((size_t)length + 1);
  if (!terminated) {
    lastError = SyscallReturnCode_OUT_OF_MEMORY;
    return FALSE;
  }
  for (int i = 0; i < length; ++i) terminated[i] = text[i];
  terminated[length] = '\0';
  const BOOL result = set_dc_colors(state) &&
                      syscall_result(UD_SYS_TextOutV1(state->handle, x, y, terminated));
  crt_free(terminated);
  return result;
}

COLORREF SetTextColor(HDC dc, COLORREF color) {
  DCState *state = find_dc(dc_handle(dc));
  if (!state) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return CLR_INVALID;
  }
  const COLORREF previous = state->textColor;
  state->textColor = color;
  if (!set_dc_colors(state)) {
    state->textColor = previous;
    return CLR_INVALID;
  }
  return previous;
}

COLORREF SetBkColor(HDC dc, COLORREF color) {
  DCState *state = find_dc(dc_handle(dc));
  if (!state) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return CLR_INVALID;
  }
  const COLORREF previous = state->backgroundColor;
  state->backgroundColor = color;
  if (!set_dc_colors(state)) {
    state->backgroundColor = previous;
    return CLR_INVALID;
  }
  return previous;
}

int SetBkMode(HDC dc, int mode) {
  DCState *state = find_dc(dc_handle(dc));
  if (!state || (mode != TRANSPARENT && mode != OPAQUE)) {
    lastError = SyscallReturnCode_INVALID_ARGUMENT;
    return 0;
  }
  const int previous = state->opaque ? OPAQUE : TRANSPARENT;
  state->opaque = mode == OPAQUE;
  if (!set_dc_colors(state)) {
    state->opaque = previous == OPAQUE;
    return 0;
  }
  return previous;
}
