#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/stdio.h>

static BOOL systemFontValid;
static UNDOS_FONT systemFont;
static UNDOS_TEXT_METRICS textMetrics;

static void recover_failed_window_creations(void) {
  for (uint32_t attempt = 0; attempt < 64; ++attempt) {
    UNDOS_WINDOW_CREATION_RECOVERY recovery = {0};
    if (!UndosGetWindowCreationRecovery(&recovery)) return;
    if (DestroyWindow(recovery.window)) continue;
    const DWORD cleanupError = GetLastError();
    const DWORD cleanupStatus = UndosGetLastStatus();
    (void)crt_printf(
        "gui: window setup error=%u status=%u; cleanup error=%u status=%u\n",
        recovery.setupError, recovery.setupStatus, cleanupError, cleanupStatus);
    return;
  }
}

static BOOL refresh_font(void) {
  UNDOS_FONT replacement;
  uint32_t generation = 0;
  UNDOS_TEXT_METRICS metrics = {0};
  BOOL acquired = UndosAcquireFont(UNDOS_FONT_SYSTEM_DEFAULT,
      UNDOS_GLYPH_FALLBACK_NOTDEF, &replacement, &generation);
  if (!acquired ||
      !UndosGetFontMetrics(replacement, UNDOS_TEXT_REGULAR,
                           UNDOS_TEXT_BASIC, &metrics) ||
      metrics.generation != generation) {
    if (acquired) (void)UndosCloseFont(replacement);
    return FALSE;
  }
  if (systemFontValid) (void)UndosCloseFont(systemFont);
  systemFont = replacement;
  systemFontValid = TRUE;
  textMetrics = metrics;
  return TRUE;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_FONTCHANGED) {
    if (refresh_font()) {
      RECT rect;
      if (GetWindowRect(window, &rect))
        (void)MoveWindow(window, rect.left, rect.top, rect.right - rect.left,
                         (int)textMetrics.lineHeight + 24, TRUE);
    }
    return 0;
  }
  if (message == WM_DESTROY) {
    PostQuitMessage(0);
    return 0;
  }
  if (message == WM_PAINT) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    if (dc) {
      (void)UndosDrawText(dc, 12, 12, UNDOS_TEXT_REGULAR,
                          UNDOS_TEXT_PROPORTIONAL, UNDOS_TEXT_BASIC,
                          "Hello from the UnDOS C SDK — café");
      (void)EndPaint(window, &paint);
    }
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}

int main(void) {
  if (!refresh_font()) return 1;
  WNDCLASS windowClass = {0};
  windowClass.lpfnWndProc = window_proc;
  windowClass.hbrBackground = UNDOS_PALETTE_BRUSH(7);
  windowClass.lpszClassName = "SDKHello";
  if (!RegisterClass(&windowClass)) {
    if (systemFontValid) (void)UndosCloseFont(systemFont);
    return 1;
  }

  HWND window = CreateWindow("SDKHello", "SDK Hello",
                             WS_VISIBLE | WS_CAPTION | WS_BORDER,
                             40, 40, 320, (int)textMetrics.lineHeight + 24,
                             NULL, NULL, NULL, NULL);
  if (!window) {
    recover_failed_window_creations();
    (void)UndosCloseFont(systemFont);
    return 1;
  }

  MSG message;
  for (;;) {
    int status = GetMessage(&message, NULL, 0, 0);
    if (status < 0) {
      if (systemFontValid) (void)UndosCloseFont(systemFont);
      (void)DestroyWindow(window);
      return 1;
    }
    if (status == 0) break;
    (void)TranslateMessage(&message);
    (void)DispatchMessage(&message);
  }
  if (systemFontValid) (void)UndosCloseFont(systemFont);
  return 0;
}
