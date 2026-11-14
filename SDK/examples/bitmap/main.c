/* WINFILE-style bitmap resources: the bitmap.bmp icon strip is linked in as
 * a resource table, loaded with LoadBitmap and blitted from a memory DC. */
#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/stdio.h>

#include "winfile_bitmap.h"

#define BITMAPS 100
#define IDS_TITLE 1

static const char title[] = "WINFILE bitmap strip";
static const UNDOS_RESOURCE resources[] = {
    {RT_BITMAP, MAKEINTRESOURCE(BITMAPS), winfileBitmap, sizeof(winfileBitmap)},
    {RT_STRING, MAKEINTRESOURCE(IDS_TITLE), title, sizeof(title)},
};

static HBITMAP strip;
static BITMAP stripInfo;

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam,
                                   LPARAM lParam) {
  if (message == WM_PAINT) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    if (dc) {
      HDC memory = CreateCompatibleDC(dc);
      if (memory) {
        HGDIOBJ previous = SelectObject(memory, strip);
        if (previous) {
          (void)BitBlt(dc, 8, 8, stripInfo.bmWidth, stripInfo.bmHeight, memory,
                       0, 0, SRCCOPY);
          (void)SelectObject(memory, previous);
        }
        (void)DeleteDC(memory);
      }
      (void)EndPaint(window, &paint);
    }
    return 0;
  }
  if (message == WM_CLOSE) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}

int main(void) {
  if (!UndosRegisterResources(resources, 2)) return 1;
  strip = LoadBitmap(NULL, MAKEINTRESOURCE(BITMAPS));
  if (!strip || !GetObject(strip, sizeof(stripInfo), &stripInfo)) return 1;
  char caption[32];
  if (!LoadString(NULL, IDS_TITLE, caption, sizeof(caption))) return 1;

  WNDCLASS klass = {0};
  klass.lpfnWndProc = window_proc;
  klass.lpszClassName = "BitmapSample";
  if (!RegisterClass(&klass)) return 1;
  HWND window = CreateWindow("BitmapSample", caption,
                             WS_VISIBLE | WS_OVERLAPPEDWINDOW, 40, 40, 240, 100,
                             NULL, NULL, NULL, NULL);
  if (!window) return 1;
  (void)crt_printf("bitmap: strip=%ldx%ld\n", (long)stripInfo.bmWidth,
                   (long)stripInfo.bmHeight);

  MSG message;
  int received;
  while ((received = GetMessage(&message, NULL, 0, 0)) > 0) {
    (void)TranslateMessage(&message);
    (void)DispatchMessage(&message);
  }
  (void)DestroyWindow(window);
  (void)DeleteObject(strip);
  return received < 0 ? 1 : 0;
}
