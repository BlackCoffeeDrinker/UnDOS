#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/controls.h"
#include "private/ui_internal.h"
#include "private/win_internal.h"

/* The MDICLIENT class, DefFrameProc and DefMDIChildProc. Children are
   ordinary WS_CHILD captioned windows of the client; their control IDs are
   idFirstChild + creation index, kept contiguous as in Win32, and the first
   UNDOS_MDI_MENU_WINDOWS of them are listed at the end of the Window menu. */

#define UI_MDI_CLIENT_LIMIT 4
#define UI_MDI_CHILD_LIMIT 32

typedef struct MdiClient {
  BOOL used;
  uint32_t handle;
  HMENU windowMenu;
  UINT firstId;
  uint32_t children[UI_MDI_CHILD_LIMIT];
  RECT rects[UI_MDI_CHILD_LIMIT];
  UINT count;
  uint32_t active;
  BOOL maximized;
  RECT restore;
  UINT menuItems;
  int separator;
} MdiClient;

static MdiClient clients[UI_MDI_CLIENT_LIMIT];

void ui_mdi_reset(void) { memset(clients, 0, sizeof(clients)); }

static MdiClient *client_record(HWND window) {
  const uint32_t handle = hwnd_value(window);
  if (!handle) return NULL;
  for (size_t i = 0; i < UI_MDI_CLIENT_LIMIT; ++i)
    if (clients[i].used && clients[i].handle == handle) return &clients[i];
  return NULL;
}

static int child_index(const MdiClient *client, uint32_t child) {
  for (UINT i = 0; i < client->count; ++i)
    if (client->children[i] == child) return (int)i;
  return -1;
}

static MdiClient *owner_of(uint32_t child, int *index) {
  for (size_t i = 0; i < UI_MDI_CLIENT_LIMIT; ++i) {
    if (!clients[i].used) continue;
    const int found = child_index(&clients[i], child);
    if (found >= 0) {
      if (index) *index = found;
      return &clients[i];
    }
  }
  return NULL;
}

void ui_mdi_attach(HWND window, const void *parameter) {
  WindowRecord *record = window_by_handle(hwnd_value(window));
  if (!record || record->procedure != ui_mdiclient_proc) return;
  MdiClient *client = NULL;
  for (size_t i = 0; i < UI_MDI_CLIENT_LIMIT && !client; ++i)
    if (!clients[i].used) client = &clients[i];
  if (!client) return;
  memset(client, 0, sizeof(*client));
  client->used = TRUE;
  client->handle = record->handle;
  client->separator = -1;
  client->firstId = 0xFF00;
  if (parameter) {
    const CLIENTCREATESTRUCT *create = (const CLIENTCREATESTRUCT *)parameter;
    client->windowMenu = create->hWindowMenu;
    client->firstId = create->idFirstChild;
  }
}

static void append_number(char *text, size_t *length, UINT value) {
  char digits[12];
  int count = 0;
  do {
    digits[count++] = (char)('0' + value % 10);
    value /= 10;
  } while (value && count < 11);
  while (count) text[(*length)++] = digits[--count];
}

static void refresh_menu(MdiClient *client) {
  HMENU menu = client->windowMenu;
  if (!menu) return;
  const DWORD error = GetLastError();
  for (UINT i = 0; i < client->menuItems; ++i)
    (void)RemoveMenu(menu, client->firstId + i, MF_BYCOMMAND);
  if (client->separator >= 0)
    (void)RemoveMenu(menu, (UINT)client->separator, MF_BYPOSITION);
  client->menuItems = 0;
  client->separator = -1;
  if (client->count) {
    const int position = GetMenuItemCount(menu);
    if (position >= 0 && AppendMenu(menu, MF_SEPARATOR, 0, NULL))
      client->separator = position;
  }
  for (UINT i = 0; i < client->count && i < UNDOS_MDI_MENU_WINDOWS; ++i) {
    char text[UI_TITLE_BYTES + 8];
    size_t length = 0;
    text[length++] = '&';
    append_number(text, &length, i + 1);
    text[length++] = ' ';
    const WindowRecord *child = window_by_handle(client->children[i]);
    ui_copy_text(text + length, sizeof(text) - length,
                 child && child->title[0] ? child->title : "(untitled)");
    const UINT flags = client->children[i] == client->active ? MF_CHECKED : 0;
    if (!AppendMenu(menu, MF_STRING | flags, client->firstId + i, text)) break;
    ++client->menuItems;
  }
  SetLastError(error);
}

static void place(MdiClient *client, UINT index, const RECT *rect) {
  client->rects[index] = *rect;
  (void)MoveWindow(hwnd_from_value(client->children[index]), rect->left,
                   rect->top, rect->right - rect->left,
                   rect->bottom - rect->top, TRUE);
}

static void client_area(const MdiClient *client, RECT *area) {
  if (!GetClientRect(hwnd_from_value(client->handle), area))
    SetRect(area, 0, 0, 0, 0);
  OffsetRect(area, -area->left, -area->top);
}

static void maximize(MdiClient *client, uint32_t child) {
  const int index = child_index(client, child);
  if (index < 0) return;
  RECT area;
  client_area(client, &area);
  const RECT saved = client->rects[index];
  place(client, (UINT)index, &area);
  client->rects[index] = saved;
  client->restore = saved;
  client->maximized = TRUE;
}

static void restore(MdiClient *client) {
  if (!client->maximized) return;
  client->maximized = FALSE;
  const int index = child_index(client, client->active);
  if (index >= 0) place(client, (UINT)index, &client->restore);
}

static void activate(MdiClient *client, uint32_t child) {
  if (child_index(client, child) < 0 && child) return;
  const uint32_t previous = client->active;
  if (previous == child) return;
  const BOOL wasMaximized = client->maximized;
  if (wasMaximized) restore(client);
  client->active = child;
  if (previous)
    (void)SendMessage(hwnd_from_value(previous), WM_MDIACTIVATE,
                      (WPARAM)hwnd_from_value(previous),
                      (LPARAM)hwnd_from_value(child));
  if (child) {
    (void)SendMessage(hwnd_from_value(child), WM_MDIACTIVATE,
                      (WPARAM)hwnd_from_value(previous),
                      (LPARAM)hwnd_from_value(child));
    if (wasMaximized) maximize(client, child);
    (void)SetFocus(hwnd_from_value(child));
  }
  refresh_menu(client);
}

static void remove_child(MdiClient *client, int index) {
  for (UINT i = (UINT)index; i + 1 < client->count; ++i) {
    client->children[i] = client->children[i + 1];
    client->rects[i] = client->rects[i + 1];
  }
  --client->count;
  for (UINT i = (UINT)index; i < client->count; ++i) {
    WindowRecord *record = window_by_handle(client->children[i]);
    if (record) record->id = client->firstId + i;
  }
}

void ui_mdi_forget(uint32_t handle) {
  MdiClient *client = client_record(hwnd_from_value(handle));
  if (client) {
    client->used = FALSE;
    return;
  }
  int index;
  client = owner_of(handle, &index);
  if (!client) return;
  const BOOL wasActive = client->active == handle;
  remove_child(client, index);
  if (wasActive) {
    client->active = 0;
    client->maximized = FALSE;
    if (client->count) activate(client, client->children[client->count - 1]);
  }
  refresh_menu(client);
}

void ui_mdi_retitle(uint32_t handle) {
  MdiClient *client = owner_of(handle, NULL);
  if (client) refresh_menu(client);
}

static HWND create_child(MdiClient *client, const MDICREATESTRUCT *create) {
  if (!create || !create->szClass) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  if (client->count == UI_MDI_CHILD_LIMIT) {
    local_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  RECT area;
  client_area(client, &area);
  const int step = GetSystemMetrics(SM_CYCAPTION) + 2;
  const int slot = (int)(client->count % 8);
  int x = create->x, y = create->y, cx = create->cx, cy = create->cy;
  if (x == CW_USEDEFAULT) x = slot * step;
  if (y == CW_USEDEFAULT) y = slot * step;
  // As in Win32, a default cx means a default cy too.
  if (cx == CW_USEDEFAULT) cx = area.right * 3 / 4, cy = CW_USEDEFAULT;
  if (cy == CW_USEDEFAULT) cy = area.bottom * 3 / 4;
  const DWORD style = WS_CHILD | WS_VISIBLE | WS_CAPTION | WS_BORDER |
                      (create->style & (WS_DISABLED | WS_VSCROLL | WS_HSCROLL));
  const UINT id = client->firstId + client->count;
  HWND child = CreateWindow(create->szClass, create->szTitle, style, x, y, cx,
                            cy, hwnd_from_value(client->handle),
                            (HMENU)(uintptr_t)id, create->hOwner,
                            (LPVOID)create);
  if (!child) return NULL;
  const UINT index = client->count++;
  client->children[index] = hwnd_value(child);
  SetRect(&client->rects[index], x, y, x + cx, y + cy);
  activate(client, hwnd_value(child));
  refresh_menu(client);
  if (create->style & WS_MAXIMIZE) maximize(client, hwnd_value(child));
  return child;
}

static void cascade(MdiClient *client) {
  restore(client);
  RECT area;
  client_area(client, &area);
  const int step = GetSystemMetrics(SM_CYCAPTION) + 2;
  const int n = (int)client->count;
  int width = area.right - step * (n - 1), height = area.bottom - step * (n - 1);
  if (width < area.right / 2) width = area.right / 2;
  if (height < area.bottom / 2) height = area.bottom / 2;
  // The active window is placed last so that it ends up on top.
  int slot = 0;
  for (int pass = 0; pass < 2; ++pass) {
    for (UINT i = 0; i < client->count; ++i) {
      const BOOL isActive = client->children[i] == client->active;
      if (isActive != (pass == 1)) continue;
      RECT rect;
      const int offset = (slot++ % 8) * step;
      SetRect(&rect, offset, offset, offset + width, offset + height);
      place(client, i, &rect);
    }
  }
}

static UINT isqrt(UINT n) {
  UINT root = 0;
  while ((root + 1) * (root + 1) <= n) ++root;
  return root;
}

static void tile(MdiClient *client, UINT how) {
  restore(client);
  if (!client->count) return;
  RECT area;
  client_area(client, &area);
  const UINT n = client->count;
  const BOOL horizontal = (how & MDITILE_HORIZONTAL) != 0;
  // Major lanes: columns for vertical tiling, rows for horizontal.
  const UINT lanes = n < 4 ? n : isqrt(n);
  const LONG extent = horizontal ? area.bottom : area.right;
  const LONG across = horizontal ? area.right : area.bottom;
  UINT next = 0;
  for (UINT lane = 0; lane < lanes; ++lane) {
    const UINT inLane = n / lanes + (lane >= lanes - n % lanes ? 1 : 0);
    const LONG start = extent * (LONG)lane / (LONG)lanes;
    const LONG end = extent * (LONG)(lane + 1) / (LONG)lanes;
    for (UINT k = 0; k < inLane && next < n; ++k, ++next) {
      const LONG a = across * (LONG)k / (LONG)inLane;
      const LONG b = across * (LONG)(k + 1) / (LONG)inLane;
      RECT rect;
      if (horizontal) SetRect(&rect, a, start, b, end);
      else SetRect(&rect, start, a, end, b);
      place(client, next, &rect);
    }
  }
}

static uint32_t neighbour(const MdiClient *client, uint32_t from,
                          BOOL previous) {
  if (!client->count) return 0;
  int index = child_index(client, from);
  if (index < 0) index = 0;
  const int n = (int)client->count;
  index = (index + (previous ? n - 1 : 1)) % n;
  return client->children[index];
}

LRESULT CALLBACK ui_mdiclient_proc(HWND window, UINT message, WPARAM wParam,
                                   LPARAM lParam) {
  MdiClient *client = client_record(window);
  if (!client) return DefWindowProc(window, message, wParam, lParam);
  switch (message) {
    case WM_MDICREATE:
      return (LRESULT)(uintptr_t)create_child(
          client, (const MDICREATESTRUCT *)(uintptr_t)lParam);
    case WM_MDIDESTROY: {
      HWND child = (HWND)(uintptr_t)wParam;
      if (child_index(client, hwnd_value(child)) >= 0)
        (void)DestroyWindow(child);
      return 0;
    }
    case WM_MDIACTIVATE:
      activate(client, hwnd_value((HWND)(uintptr_t)wParam));
      return 0;
    case WM_MDIGETACTIVE:
      if (lParam) *(BOOL *)(uintptr_t)lParam = client->maximized;
      return (LRESULT)(uintptr_t)hwnd_from_value(client->active);
    case WM_MDINEXT: {
      const uint32_t from =
          wParam ? hwnd_value((HWND)(uintptr_t)wParam) : client->active;
      activate(client, neighbour(client, from, lParam != 0));
      return 0;
    }
    case WM_MDIMAXIMIZE: {
      const uint32_t child = hwnd_value((HWND)(uintptr_t)wParam);
      if (child_index(client, child) < 0) return 0;
      if (child != client->active) activate(client, child);
      if (!client->maximized) maximize(client, child);
      return 0;
    }
    case WM_MDIRESTORE:
      restore(client);
      return 0;
    case WM_MDICASCADE:
      cascade(client);
      return client->count != 0;
    case WM_MDITILE:
      tile(client, (UINT)wParam);
      return client->count != 0;
    case WM_MDIICONARRANGE:
      return 0;
    case WM_MDISETMENU: {
      WindowRecord *record = window_by_handle(client->handle);
      HWND frame = record ? hwnd_from_value(record->parent) : NULL;
      HMENU previous = frame ? GetMenu(frame) : NULL;
      HMENU windowMenu = (HMENU)(uintptr_t)lParam;
      if (windowMenu && windowMenu != client->windowMenu) {
        HMENU old = client->windowMenu;
        client->windowMenu = NULL;
        if (old) {
          const DWORD error = GetLastError();
          for (UINT i = 0; i < client->menuItems; ++i)
            (void)RemoveMenu(old, client->firstId + i, MF_BYCOMMAND);
          if (client->separator >= 0)
            (void)RemoveMenu(old, (UINT)client->separator, MF_BYPOSITION);
          SetLastError(error);
        }
        client->menuItems = 0;
        client->separator = -1;
        client->windowMenu = windowMenu;
      }
      if (wParam && frame) (void)SetMenu(frame, (HMENU)(uintptr_t)wParam);
      refresh_menu(client);
      return (LRESULT)(uintptr_t)previous;
    }
    case WM_MDIREFRESHMENU:
      refresh_menu(client);
      return 0;
    case WM_SETFOCUS:
      if (client->active) (void)SetFocus(hwnd_from_value(client->active));
      return 0;
    case WM_SIZE:
      if (client->maximized && client->active) {
        const uint32_t active = client->active;
        client->maximized = FALSE;
        const int index = child_index(client, active);
        if (index >= 0) client->rects[index] = client->restore;
        maximize(client, active);
      }
      return 0;
    default:
      return DefWindowProc(window, message, wParam, lParam);
  }
}

LRESULT DefFrameProc(HWND frame, HWND client, UINT message, WPARAM wParam,
                     LPARAM lParam) {
  MdiClient *record = client_record(client);
  if (record) {
    switch (message) {
      case WM_COMMAND: {
        const UINT id = LOWORD(wParam);
        if (id >= record->firstId && id < record->firstId + record->count) {
          activate(record, record->children[id - record->firstId]);
          return 0;
        }
        break;
      }
      case WM_SIZE: {
        RECT area;
        if (GetClientRect(frame, &area))
          (void)MoveWindow(client, 0, 0, area.right - area.left,
                           area.bottom - area.top, TRUE);
        (void)SendMessage(client, WM_SIZE, wParam, lParam);
        return 0;
      }
      case WM_SETFOCUS:
        (void)SetFocus(client);
        return 0;
      default: break;
    }
  }
  return DefWindowProc(frame, message, wParam, lParam);
}

LRESULT DefMDIChildProc(HWND child, UINT message, WPARAM wParam,
                        LPARAM lParam) {
  int index;
  MdiClient *client = owner_of(hwnd_value(child), &index);
  if (client) {
    const HWND clientWindow = hwnd_from_value(client->handle);
    switch (message) {
      case WM_CLOSE:
        (void)SendMessage(clientWindow, WM_MDIDESTROY, (WPARAM)child, 0);
        return 0;
      case WM_CHILDACTIVATE:
      case WM_LBUTTONDOWN:
      case WM_RBUTTONDOWN:
        if (client->active != hwnd_value(child)) activate(client, hwnd_value(child));
        break;
      case WM_SYSCOMMAND: {
        const UINT command = (UINT)wParam & 0xFFF0u;
        if (command == SC_MAXIMIZE) {
          (void)SendMessage(clientWindow, WM_MDIMAXIMIZE, (WPARAM)child, 0);
          return 0;
        }
        if (command == SC_RESTORE) {
          (void)SendMessage(clientWindow, WM_MDIRESTORE, (WPARAM)child, 0);
          return 0;
        }
        if (command == SC_NEXTWINDOW || command == SC_PREVWINDOW) {
          (void)SendMessage(clientWindow, WM_MDINEXT, (WPARAM)child,
                            command == SC_PREVWINDOW);
          return 0;
        }
        if (command == SC_CLOSE) {
          (void)SendMessage(child, WM_CLOSE, 0, 0);
          return 0;
        }
        break;
      }
      case WM_SETTEXT: {
        WindowRecord *record = window_by_handle(hwnd_value(child));
        if (record) ui_copy_text(record->title, sizeof(record->title),
                                 (const char *)(uintptr_t)lParam);
        refresh_menu(client);
        return TRUE;
      }
      default: break;
    }
  }
  return DefWindowProc(child, message, wParam, lParam);
}

BOOL TranslateMDISysAccel(HWND clientWindow, MSG *message) {
  MdiClient *client = client_record(clientWindow);
  if (!client || !message || !client->active) return FALSE;
  if (message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN)
    return FALSE;
  if (!(GetKeyState(VK_CONTROL) & 0x8000)) return FALSE;
  const HWND active = hwnd_from_value(client->active);
  if (message->wParam == VK_F4) {
    (void)SendMessage(active, WM_SYSCOMMAND, SC_CLOSE, 0);
    return TRUE;
  }
  if (message->wParam == VK_F6 || message->wParam == VK_TAB) {
    const BOOL back = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    (void)SendMessage(active, WM_SYSCOMMAND,
                      back ? SC_PREVWINDOW : SC_NEXTWINDOW, 0);
    return TRUE;
  }
  return FALSE;
}

HWND CreateMDIWindow(LPCSTR className, LPCSTR title, DWORD style, int x,
                     int y, int width, int height, HWND client,
                     HINSTANCE instance, LPARAM lParam) {
  if (!client_record(client)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  MDICREATESTRUCT create = {className, title, instance, x, y, width, height,
                            style, lParam};
  return (HWND)(uintptr_t)SendMessage(client, WM_MDICREATE, 0,
                                      (LPARAM)(uintptr_t)&create);
}
