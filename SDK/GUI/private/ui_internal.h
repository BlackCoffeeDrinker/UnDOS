#ifndef UNDOSGUI_PRIVATE_UI_INTERNAL_H
#define UNDOSGUI_PRIVATE_UI_INTERNAL_H

#include <undosgui/extensions.h>

#define UI_CONTEXT_MAGIC UINT32_C(0x55494358)

int ui_core_window_alive(HWND window);
int ui_core_window_in_family(HWND window, HWND owner);
uint32_t ui_core_window_parent(HWND window);
UNDOS_UI_CONTEXT *ui_core_window_context(HWND window);
int ui_core_attach_context(HWND window, UNDOS_UI_CONTEXT *context);
int ui_core_mark_ui_recovery(HWND window, DWORD operationError,
                             DWORD operationStatus, DWORD cleanupError,
                             DWORD cleanupStatus);
int ui_core_retain_font(HWND window, UNDOS_FONT font);
int ui_core_dc_uses_font(HDC dc, UNDOS_FONT font);
void ui_default_system_menu(HWND window, LPARAM hitPoint);
void ui_default_system_geometry(HWND window, UINT command);
void ui_core_failure(uint32_t status, DWORD error);
DWORD ui_core_error_from_status(uint32_t status);

#endif
