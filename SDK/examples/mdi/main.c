/* WINFILE-style MDI frame: an MDICLIENT with directory windows, a Window
 * menu that lists them, Cascade/Tile commands and a one-shot timer. */
#include <windows.h>
#include <undoscrt/stdio.h>

#define IDM_CASCADE 1
#define IDM_TILE 2
#define IDM_NEWWINDOW 3
#define IDM_EXIT 4
#define IDM_CHILDSTART 100
#define ID_STARTUP_TIMER 1

static HWND client;
static int opened;

static LRESULT CALLBACK child_proc(HWND window, UINT message, WPARAM wParam,
                                   LPARAM lParam) {
  if (message == WM_PAINT) {
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(window, &paint);
    char title[64];
    GetWindowText(window, title, sizeof(title));
    int length = 0;
    while (title[length]) ++length;
    TextOut(dc, 4, 4, title, length);
    EndPaint(window, &paint);
    return 0;
  }
  return DefMDIChildProc(window, message, wParam, lParam);
}

static void open_window(const char *path) {
  HWND child = CreateMDIWindow("MdiDirectory", path, 0, CW_USEDEFAULT,
                               CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                               client, NULL, 0);
  if (child) ++opened;
}

static LRESULT CALLBACK frame_proc(HWND window, UINT message, WPARAM wParam,
                                   LPARAM lParam) {
  switch (message) {
    case WM_TIMER:
      KillTimer(window, ID_STARTUP_TIMER);
      SendMessage(client, WM_MDITILE, MDITILE_VERTICAL, 0);
      (void)crt_printf("mdi: ready windows=%d\n", opened);
      return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam)) {
        case IDM_CASCADE: SendMessage(client, WM_MDICASCADE, 0, 0); return 0;
        case IDM_TILE:
          SendMessage(client, WM_MDITILE, MDITILE_VERTICAL, 0);
          return 0;
        case IDM_NEWWINDOW: open_window("/"); return 0;
        case IDM_EXIT: DestroyWindow(window); return 0;
        default: break;
      }
      break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    default: break;
  }
  return DefFrameProc(window, client, message, wParam, lParam);
}

int main(void) {
  WNDCLASS frameClass = {0};
  frameClass.lpfnWndProc = frame_proc;
  frameClass.lpszClassName = "MdiFrame";
  WNDCLASS childClass = {0};
  childClass.lpfnWndProc = child_proc;
  childClass.lpszClassName = "MdiDirectory";
  if (!RegisterClass(&frameClass) || !RegisterClass(&childClass)) return 1;

  HMENU bar = CreateMenu();
  HMENU file = CreatePopupMenu();
  HMENU windowMenu = CreatePopupMenu();
  AppendMenu(file, MF_STRING, IDM_EXIT, "E&xit");
  AppendMenu(windowMenu, MF_STRING, IDM_NEWWINDOW, "&New Window");
  AppendMenu(windowMenu, MF_STRING, IDM_CASCADE, "&Cascade");
  AppendMenu(windowMenu, MF_STRING, IDM_TILE, "&Tile");
  AppendMenu(bar, MF_POPUP, (UINT_PTR)file, "&File");
  AppendMenu(bar, MF_POPUP, (UINT_PTR)windowMenu, "&Window");

  HWND frame = CreateWindow("MdiFrame", "File Manager",
                            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 20, 20, 480, 320,
                            NULL, bar, NULL, NULL);
  if (!frame) return 1;
  RECT area;
  GetClientRect(frame, &area);
  CLIENTCREATESTRUCT ccs = {windowMenu, IDM_CHILDSTART};
  client = CreateWindow("MDICLIENT", NULL, WS_CHILD | WS_VISIBLE, 0, 0,
                        area.right - area.left, area.bottom - area.top, frame,
                        (HMENU)(uintptr_t)0xCAC, NULL, &ccs);
  if (!client) return 1;
  open_window("/");
  open_window("/APPS");
  SetTimer(frame, ID_STARTUP_TIMER, 100, NULL);

  MSG message;
  while (GetMessage(&message, NULL, 0, 0) > 0) {
    if (TranslateMDISysAccel(client, &message)) continue;
    DispatchMessage(&message);
  }
  return 0;
}
