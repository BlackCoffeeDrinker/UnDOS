/* WINFILE-style menu state and accelerators: View > Name/Details radio
 * checks, a greyed command, F5 and Ctrl+D accelerators, and SendMessage. */
#include <windows.h>
#include <undoscrt/stdio.h>

#define IDM_VNAME 1001
#define IDM_VDETAILS 1002
#define IDM_FORMAT 1003
#define IDM_REFRESH 1004
#define IDM_EXIT 1005
#define WM_APP_QUERY (WM_USER + 1)

static HMENU viewMenu;
static UINT activeView = IDM_VNAME;

static void select_view(HMENU menu, UINT view) {
  (void)CheckMenuItem(menu, activeView, MF_BYCOMMAND | MF_UNCHECKED);
  (void)CheckMenuItem(menu, view, MF_BYCOMMAND | MF_CHECKED);
  activeView = view;
}

static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM wParam,
                                   LPARAM lParam) {
  if (message == WM_APP_QUERY) return (LRESULT)activeView;
  if (message == WM_COMMAND) {
    const UINT command = LOWORD(wParam);
    if (command == IDM_VNAME || command == IDM_VDETAILS)
      select_view(GetMenu(window), command);
    else if (command == IDM_REFRESH)
      (void)InvalidateRect(window, NULL, TRUE);
    else if (command == IDM_EXIT)
      PostQuitMessage(0);
    return 0;
  }
  if (message == WM_CLOSE) {
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(window, message, wParam, lParam);
}

static HMENU build_menu(void) {
  HMENU bar = CreateMenu();
  HMENU file = CreatePopupMenu();
  viewMenu = CreatePopupMenu();
  if (!bar || !file || !viewMenu) return NULL;
  if (!AppendMenu(file, MF_STRING | MF_GRAYED, IDM_FORMAT, "Format Disk...") ||
      !AppendMenu(file, MF_SEPARATOR, 0, NULL) ||
      !AppendMenu(file, MF_STRING, IDM_EXIT, "Exit") ||
      !AppendMenu(viewMenu, MF_STRING | MF_CHECKED, IDM_VNAME, "Name") ||
      !AppendMenu(viewMenu, MF_STRING, IDM_VDETAILS, "All File Details\tCtrl+D") ||
      !AppendMenu(viewMenu, MF_SEPARATOR, 0, NULL) ||
      !AppendMenu(viewMenu, MF_STRING, IDM_REFRESH, "Refresh\tF5") ||
      !AppendMenu(bar, MF_POPUP, (UINT_PTR)file, "File") ||
      !AppendMenu(bar, MF_POPUP, (UINT_PTR)viewMenu, "View")) {
    (void)DestroyMenu(bar);
    return NULL;
  }
  return bar;
}

int main(void) {
  WNDCLASS klass = {0};
  klass.lpfnWndProc = window_proc;
  klass.lpszClassName = "MenuSample";
  if (!RegisterClass(&klass)) return 1;
  HWND window = CreateWindow("MenuSample", "Menu sample",
                             WS_VISIBLE | WS_OVERLAPPEDWINDOW, 40, 40, 320,
                             160, NULL, NULL, NULL, NULL);
  if (!window) return 1;
  HMENU bar = build_menu();
  if (!bar || !SetMenu(window, bar)) return 1;

  ACCEL keys[] = {{FVIRTKEY, VK_F5, IDM_REFRESH},
                  {FVIRTKEY | FCONTROL, 'D', IDM_VDETAILS}};
  HACCEL accelerators = CreateAcceleratorTable(keys, 2);
  if (!accelerators) return 1;

  (void)SendMessage(window, WM_COMMAND, MAKEWPARAM(IDM_VDETAILS, 0), 0);
  const BOOL detailsChecked =
      (GetMenuState(viewMenu, IDM_VDETAILS, MF_BYCOMMAND) & MF_CHECKED) != 0;
  const BOOL formatGrayed =
      (GetMenuState(GetSubMenu(bar, 0), 0, MF_BYPOSITION) & MF_GRAYED) != 0;
  (void)crt_printf("menu: view=%u details=%d grayed=%d\n",
                   (unsigned)SendMessage(window, WM_APP_QUERY, 0, 0),
                   detailsChecked, formatGrayed);

  MSG message;
  int received;
  while ((received = GetMessage(&message, NULL, 0, 0)) > 0) {
    if (TranslateAccelerator(window, accelerators, &message)) continue;
    (void)TranslateMessage(&message);
    (void)DispatchMessage(&message);
  }
  (void)DestroyAcceleratorTable(accelerators);
  (void)DestroyWindow(window);
  (void)DestroyMenu(bar);
  return received < 0 ? 1 : 0;
}
