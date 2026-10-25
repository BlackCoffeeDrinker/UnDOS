#include <windows.h>

static BOOL running = TRUE;
static HFONT systemFont;
static struct TextMetricsV2 textMetrics;

static BOOL refresh_font(void) {
  HFONT replacement = 0;
  uint32_t generation = 0;
  struct TextMetricsV2 metrics = {0};
  if (!AcquireFont(FontSource_AcquireSystemDefault, GlyphFallbackPolicy_FallbackFaceNotdef,
                  &replacement, &generation) ||
      !GetFontMetrics(replacement, TextStyle_StyleRegular,
                      TextRepertoire_RepertoireBasic, &metrics) ||
      metrics.generation != generation) {
    if (replacement) (void)CloseFont(replacement);
    return FALSE;
  }
  if (systemFont) (void)CloseFont(systemFont);
  systemFont = replacement;
  textMetrics = metrics;
  return TRUE;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_CLOSE) {
    running = FALSE;
    return 0;
  }
  if (message == WM_FONTCHANGED) {
    if (refresh_font()) {
      RECT rect;
      if (GetWindowRect(window, &rect))
        (void)MoveWindow(window, rect.left, rect.top, rect.right - rect.left,
                         (int)textMetrics.lineHeight + 24, TRUE);
    }
    return 0;
  }
  if (message == WM_PAINT) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    if (dc) {
      (void)DrawText(dc, 12, 12, TextStyle_StyleRegular,
                     TextLayoutMode_LayoutProportional,
                     TextRepertoire_RepertoireBasic,
                     "Hello from the UnDOS C SDK!");
      (void)EndPaint(window, &paint);
    }
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}

int main(void) {
  if (!refresh_font()) return 1;
  WNDCLASSA windowClass = {0};
  windowClass.lpfnWndProc = window_proc;
  windowClass.hbrBackground = (HBRUSH)(uintptr_t)7;
  windowClass.lpszClassName = "SDKHello";
  if (!RegisterClass(&windowClass)) return 1;

  HWND window = CreateWindow("SDKHello", "SDK Hello",
                             WS_VISIBLE | WS_CAPTION | WS_BORDER,
                             40, 40, 320, (int)textMetrics.lineHeight + 24,
                             NULL, NULL, NULL, NULL);
  if (!window) {
    (void)CloseFont(systemFont);
    return 1;
  }

  MSG message;
  int status;
  while (running && (status = GetMessage(&message, NULL, 0, 0)) > 0) {
    (void)TranslateMessage(&message);
    (void)DispatchMessage(&message);
  }
  if (status < 0) return 1;
  (void)CloseFont(systemFont);
  return DestroyWindow(window) ? 0 : 1;
}
