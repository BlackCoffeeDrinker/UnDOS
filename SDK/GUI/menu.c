#include <windows.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"
#include "private/win_internal.h"

#define UI_ACCEL_TABLE_LIMIT 8
#define UI_ACCEL_ENTRY_LIMIT 64
#define UI_MENU_STATE_BITS (MF_GRAYED | MF_DISABLED | MF_CHECKED)

typedef struct AccelTable {
  BOOL used;
  uint16_t generation;
  int count;
  ACCEL entries[UI_ACCEL_ENTRY_LIMIT];
} AccelTable;

static AccelTable accelTables[UI_ACCEL_TABLE_LIMIT];

#ifdef UNDOSGUI_TESTING
void undosgui_test_reset_menu(void) {
  memset(accelTables, 0, sizeof(accelTables));
}
#endif

static uint32_t menu_value(HMENU menu) {
  return (uint32_t)(uintptr_t)menu;
}

static BOOL menu_state(HMENU menu, UINT item, UINT flags, UINT mask,
                       UINT state, UINT *previous, UINT *itemId) {
  if (!menu) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_MENU_HANDLE);
    return FALSE;
  }
  uint32_t oldFlags = 0, identifier = 0;
  const uint32_t status = ui_backend_modify_menu_state(
      menu_value(menu), item, (flags & MF_BYPOSITION) ? 1u : 0u, mask, state,
      &oldFlags, &identifier);
  if (status != UI_STATUS_OK) {
    DWORD error = ui_core_error_from_status(status);
    if (status == UI_STATUS_NOT_FOUND) error = ERROR_MENU_ITEM_NOT_FOUND;
    else if (status == UI_STATUS_INVALID_ARGUMENT && !mask)
      error = ERROR_INVALID_MENU_HANDLE;
    ui_core_failure(status, error);
    return FALSE;
  }
  if (previous) *previous = oldFlags;
  if (itemId) *itemId = identifier;
  return TRUE;
}

DWORD CheckMenuItem(HMENU menu, UINT item, UINT flags) {
  if (flags & ~(MF_BYPOSITION | MF_CHECKED)) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return (DWORD)-1;
  }
  UINT previous = 0;
  if (!menu_state(menu, item, flags, MF_CHECKED, flags & MF_CHECKED,
                  &previous, NULL))
    return (DWORD)-1;
  return previous & MF_CHECKED;
}

BOOL EnableMenuItem(HMENU menu, UINT item, UINT flags) {
  if (flags & ~(MF_BYPOSITION | MF_GRAYED | MF_DISABLED)) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return -1;
  }
  UINT previous = 0;
  if (!menu_state(menu, item, flags, MF_GRAYED | MF_DISABLED,
                  flags & (MF_GRAYED | MF_DISABLED), &previous, NULL))
    return -1;
  return (BOOL)(previous & (MF_GRAYED | MF_DISABLED));
}

UINT GetMenuState(HMENU menu, UINT item, UINT flags) {
  UINT previous = 0;
  if (!menu_state(menu, item, flags, 0, 0, &previous, NULL)) return (UINT)-1;
  return previous & (UI_MENU_STATE_BITS | MF_POPUP | MF_SEPARATOR);
}

HMENU GetSubMenu(HMENU menu, int position) {
  if (position < 0) return NULL;
  UINT previous = 0, identifier = 0;
  if (!menu_state(menu, (UINT)position, MF_BYPOSITION, 0, 0, &previous,
                  &identifier))
    return NULL;
  if (!(previous & MF_POPUP)) return NULL;
  return (HMENU)(uintptr_t)identifier;
}

UINT GetMenuItemID(HMENU menu, int position) {
  if (position < 0) return (UINT)-1;
  UINT previous = 0, identifier = 0;
  if (!menu_state(menu, (UINT)position, MF_BYPOSITION, 0, 0, &previous,
                  &identifier))
    return (UINT)-1;
  if (previous & MF_POPUP) return (UINT)-1;
  if (previous & MF_SEPARATOR) return 0;
  return identifier;
}

int GetMenuItemCount(HMENU menu) {
  int count = 0;
  for (;;) {
    uint32_t oldFlags = 0, identifier = 0;
    const uint32_t status = ui_backend_modify_menu_state(
        menu_value(menu), (uint32_t)count, 1, 0, 0, &oldFlags, &identifier);
    if (status == UI_STATUS_NOT_FOUND) return count;
    if (status != UI_STATUS_OK) {
      ui_core_failure(status, status == UI_STATUS_INVALID_ARGUMENT
                                  ? ERROR_INVALID_MENU_HANDLE
                                  : ui_core_error_from_status(status));
      return -1;
    }
    ++count;
  }
}

static AccelTable *accel_lookup(HACCEL table) {
  const uintptr_t value = (uintptr_t)table;
  const uintptr_t index = (value & 0xFFFFu) - 1u;
  if (!value || index >= UI_ACCEL_TABLE_LIMIT) return NULL;
  AccelTable *record = &accelTables[index];
  if (!record->used || record->generation != (uint16_t)(value >> 16)) return NULL;
  return record;
}

HACCEL CreateAcceleratorTable(LPACCEL entries, int count) {
  if (!entries || count <= 0) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  if (count > UI_ACCEL_ENTRY_LIMIT) {
    ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  const BYTE known = FVIRTKEY | FNOINVERT | FSHIFT | FCONTROL | FALT;
  for (int i = 0; i < count; ++i) {
    if (entries[i].fVirt & ~known) {
      ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
      return NULL;
    }
  }
  for (uintptr_t i = 0; i < UI_ACCEL_TABLE_LIMIT; ++i) {
    AccelTable *record = &accelTables[i];
    if (record->used) continue;
    if (++record->generation == 0) record->generation = 1;
    record->used = TRUE;
    record->count = count;
    memcpy(record->entries, entries, (size_t)count * sizeof(ACCEL));
    return (HACCEL)(((uintptr_t)record->generation << 16) | (i + 1));
  }
  ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
  return NULL;
}

BOOL DestroyAcceleratorTable(HACCEL table) {
  AccelTable *record = accel_lookup(table);
  if (!record) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_ACCEL_HANDLE);
    return FALSE;
  }
  record->used = FALSE;
  record->count = 0;
  return TRUE;
}

static BOOL key_down(int key) {
  return (GetKeyState(key) & 0x8000) != 0;
}

static BYTE accel_modifiers(void) {
  BYTE modifiers = 0;
  if (key_down(VK_SHIFT)) modifiers |= FSHIFT;
  if (key_down(VK_CONTROL)) modifiers |= FCONTROL;
  if (key_down(VK_MENU)) modifiers |= FALT;
  return modifiers;
}

static BOOL accel_matches(const ACCEL *entry, const MSG *message,
                          BYTE modifiers) {
  const BYTE wanted = entry->fVirt & (FSHIFT | FCONTROL | FALT);
  if (entry->fVirt & FVIRTKEY) {
    if (message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN)
      return FALSE;
    return (WORD)message->wParam == entry->key && wanted == modifiers;
  }
  if (message->message != WM_CHAR && message->message != WM_SYSCHAR)
    return FALSE;
  if ((WORD)message->wParam != entry->key) return FALSE;
  // Character accelerators ignore Shift because it already shaped the char.
  return (wanted & (FCONTROL | FALT)) == (modifiers & (FCONTROL | FALT));
}

int TranslateAccelerator(HWND window, HACCEL table, MSG *message) {
  AccelTable *record = accel_lookup(table);
  if (!record) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_ACCEL_HANDLE);
    return 0;
  }
  if (!message || !window) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  if (message->message != WM_KEYDOWN && message->message != WM_SYSKEYDOWN &&
      message->message != WM_CHAR && message->message != WM_SYSCHAR)
    return 0;
  const BYTE modifiers = accel_modifiers();
  for (int i = 0; i < record->count; ++i) {
    const ACCEL *entry = &record->entries[i];
    if (!accel_matches(entry, message, modifiers)) continue;
    HMENU menu = GetMenu(window);
    if (menu) {
      (void)SendMessage(window, WM_INITMENU, (WPARAM)(uintptr_t)menu, 0);
      UINT previous = 0;
      if (menu_state(menu, entry->cmd, MF_BYCOMMAND, 0, 0, &previous, NULL) &&
          (previous & (MF_GRAYED | MF_DISABLED)))
        return 1;
    }
    (void)SendMessage(window, WM_COMMAND, MAKEWPARAM(entry->cmd, 1), 0);
    return 1;
  }
  return 0;
}

HMENU CreateMenu(void) {
  uint32_t menu = 0;
  if (!backend_ok(ui_backend_create_menu(0, &menu))) return NULL;
  return (HMENU)(uintptr_t)menu;
}

HMENU CreatePopupMenu(void) {
  uint32_t menu = 0;
  if (!backend_ok(ui_backend_create_menu(1, &menu))) return NULL;
  return (HMENU)(uintptr_t)menu;
}

BOOL DestroyMenu(HMENU menu) {
  return backend_ok(ui_backend_destroy_menu((uint32_t)(uintptr_t)menu));
}

static BOOL remove_item(HMENU menu, UINT item, UINT flags, BOOL destroy) {
  if (flags & ~MF_BYPOSITION) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  const uint32_t byPosition = (flags & MF_BYPOSITION) ? 1u : 0u;
  uint32_t oldFlags = 0, identifier = 0;
  uint32_t status = ui_backend_modify_menu_state(menu_value(menu), item,
                                                 byPosition, 0, 0, &oldFlags,
                                                 &identifier);
  if (status == UI_STATUS_OK)
    status = ui_backend_remove_menu_item(menu_value(menu), item, byPosition,
                                         &identifier);
  if (status != UI_STATUS_OK) {
    ui_core_failure(status, status == UI_STATUS_INVALID_ARGUMENT
                                ? ERROR_INVALID_MENU_HANDLE
                                : status == UI_STATUS_NOT_FOUND
                                      ? ERROR_MENU_ITEM_NOT_FOUND
                                      : ui_core_error_from_status(status));
    return FALSE;
  }
  if (destroy && (oldFlags & UI_MENU_POPUP))
    (void)ui_backend_destroy_menu(identifier);
  return TRUE;
}

BOOL RemoveMenu(HMENU menu, UINT item, UINT flags) {
  return remove_item(menu, item, flags, FALSE);
}

BOOL DeleteMenu(HMENU menu, UINT item, UINT flags) {
  return remove_item(menu, item, flags, TRUE);
}

BOOL DrawMenuBar(HWND window) {
  // The compositor repaints an attached menu bar whenever it changes.
  if (!window_by_handle(hwnd_value(window))) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  return TRUE;
}

BOOL AppendMenu(HMENU menu, UINT flags, UINT_PTR item, LPCSTR text) {
  const UINT allowed = MF_GRAYED | MF_DISABLED | MF_CHECKED | MF_POPUP | MF_SEPARATOR;
  if (flags & ~allowed) {
    local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
    return FALSE;
  }
  if ((flags & MF_SEPARATOR) &&
      (flags & (MF_POPUP | MF_DISABLED | MF_GRAYED | MF_CHECKED))) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if ((flags & MF_POPUP) && item > UINT32_MAX) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (item > UINT32_MAX) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  if (!(flags & MF_SEPARATOR) && !utf8_string(text, UI_TITLE_BYTES, NULL, TRUE))
    return FALSE;
  if (flags & MF_SEPARATOR) text = "";
  return backend_ok(ui_backend_append_menu((uint32_t)(uintptr_t)menu, flags,
                                            (uint32_t)item, text));
}

BOOL SetMenu(HWND window, HMENU menu) {
  uint32_t handle = hwnd_value(window);
  if (!window_by_handle(handle)) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return FALSE;
  }
  return backend_ok(ui_backend_set_menu(handle, (uint32_t)(uintptr_t)menu));
}

HMENU GetMenu(HWND window) {
  uint32_t menu = 0;
  if (!window_by_handle(hwnd_value(window))) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_WINDOW_HANDLE);
    return NULL;
  }
  if (!backend_ok(ui_backend_get_menu(hwnd_value(window), &menu))) return NULL;
  return (HMENU)(uintptr_t)menu;
}

/* --- Menu templates (RT_MENU, standard MENUITEMTEMPLATE format) -------- */
#define UI_MENU_TEMPLATE_DEPTH 8
#define UI_MENU_TEXT_BYTES 128

static const WORD *menu_text(const WORD *p, const WORD *end, char *out) {
  size_t used = 0;
  while (p < end && *p) {
    WORD c = *p++;
    if (c < 0x80) {
      if (used + 1 < UI_MENU_TEXT_BYTES) out[used++] = (char)c;
    } else if (c < 0x800) {
      if (used + 2 < UI_MENU_TEXT_BYTES) {
        out[used++] = (char)(0xC0 | (c >> 6));
        out[used++] = (char)(0x80 | (c & 0x3F));
      }
    } else if (used + 3 < UI_MENU_TEXT_BYTES) {
      out[used++] = (char)(0xE0 | (c >> 12));
      out[used++] = (char)(0x80 | ((c >> 6) & 0x3F));
      out[used++] = (char)(0x80 | (c & 0x3F));
    }
  }
  out[used] = 0;
  return p < end ? p + 1 : NULL;
}

static const WORD *menu_items(HMENU menu, const WORD *p, const WORD *end, int depth) {
  char text[UI_MENU_TEXT_BYTES];
  if (depth >= UI_MENU_TEMPLATE_DEPTH) return NULL;
  for (;;) {
    WORD flags, id = 0;
    UINT append;
    if (!p || p >= end) return NULL;
    flags = *p++;
    if (!(flags & MF_POPUP)) {
      if (p >= end) return NULL;
      id = *p++;
    }
    p = menu_text(p, end, text);
    if (!p) return NULL;
    append = flags & (MF_GRAYED | MF_DISABLED | MF_CHECKED);
    if (flags & MF_POPUP) {
      HMENU sub = CreatePopupMenu();
      if (!sub) return NULL;
      p = menu_items(sub, p, end, depth + 1);
      if (!p || !AppendMenu(menu, append | MF_POPUP, (UINT_PTR)sub, text)) {
        DestroyMenu(sub);
        return NULL;
      }
    } else if (flags == 0 && id == 0 && text[0] == 0) {
      if (!AppendMenu(menu, MF_SEPARATOR, 0, NULL)) return NULL;
    } else if (!AppendMenu(menu, append, id, text)) {
      return NULL;
    }
    if (flags & MF_END) return p;
  }
}

static HMENU menu_from_template(const void *data, DWORD size) {
  const WORD *p = (const WORD *)data;
  const WORD *end;
  HMENU menu;
  if (!data || size < 4 || p[0] != 0) {
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  end = p + size / sizeof(WORD);
  p += 2 + p[1] / sizeof(WORD);
  menu = CreateMenu();
  if (!menu) return NULL;
  if (p >= end) return menu;
  if (!menu_items(menu, p, end, 0)) {
    DestroyMenu(menu);
    local_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  return menu;
}

HMENU LoadMenuIndirect(const void *menuTemplate) {
  /* No size is given: bound the walk generously; MF_END terminates it. */
  return menu_from_template(menuTemplate, 0x10000);
}

HMENU LoadMenu(HINSTANCE instance, LPCSTR name) {
  HRSRC res = FindResource(instance, name, RT_MENU);
  HGLOBAL data;
  if (!res) return NULL;
  data = LoadResource(instance, res);
  if (!data) return NULL;
  return menu_from_template(LockResource(data), SizeofResource(instance, res));
}

BOOL InsertMenu(HMENU menu, UINT position, UINT flags, UINT_PTR item, LPCSTR text) {
  /* The kernel menu only appends, so only "insert at the end" works. */
  int count;
  UINT append = flags & ~(MF_BYPOSITION | MF_BYCOMMAND);
  if (position != (UINT)-1) {
    count = GetMenuItemCount(menu);
    if (count < 0) return FALSE;
    if (!(flags & MF_BYPOSITION) || position < (UINT)count) {
      local_failure(UI_STATUS_UNSUPPORTED, ERROR_CALL_NOT_IMPLEMENTED);
      return FALSE;
    }
  }
  return AppendMenu(menu, append, item, text);
}
