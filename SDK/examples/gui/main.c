#include <windows.h>

static BOOL running = TRUE;

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
  if (message == WM_CLOSE) {
    running = FALSE;
    return 0;
  }
  if (message == WM_PAINT) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    if (dc) {
      (void)TextOut(dc, 12, 12, "Hello from the UnDOS C SDK!", 27);
      (void)EndPaint(window, &paint);
    }
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}

int main(void) {
  WNDCLASSA windowClass = {0};
  windowClass.lpfnWndProc = window_proc;
  windowClass.hbrBackground = (HBRUSH)(uintptr_t)7;
  windowClass.lpszClassName = "SDKHello";
  if (!RegisterClass(&windowClass)) return 1;

  HWND window = CreateWindow("SDKHello", "SDK Hello",
                             WS_VISIBLE | WS_CAPTION | WS_BORDER,
                             40, 40, 320, 160, NULL, NULL, NULL, NULL);
  if (!window) return 1;

  MSG message;
  int status;
  while (running && (status = GetMessage(&message, NULL, 0, 0)) > 0) {
    (void)TranslateMessage(&message);
    (void)DispatchMessage(&message);
  }
  if (status < 0) return 1;
  return DestroyWindow(window) ? 0 : 1;
}
