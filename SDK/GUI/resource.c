#include <windows.h>
#include <undosgui/extensions.h>
#include <undoscrt/string.h>

#include "private/ui_internal.h"
#include "private/win_internal.h"

#define UI_RESOURCE_TABLE_LIMIT 8
#define UI_BITMAP_FILE_HEADER_BYTES 14u

typedef struct ResourceTable {
  const UNDOS_RESOURCE *entries;
  UINT count;
} ResourceTable;

static ResourceTable tables[UI_RESOURCE_TABLE_LIMIT];

void ui_resource_reset(void) {
  memset(tables, 0, sizeof(tables));
}

BOOL UndosRegisterResources(const UNDOS_RESOURCE *table, UINT count) {
  if (!table || !count) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  for (UINT i = 0; i < UI_RESOURCE_TABLE_LIMIT; ++i) {
    if (tables[i].entries == table) {
      tables[i].count = count;
      return TRUE;
    }
  }
  for (UINT i = 0; i < UI_RESOURCE_TABLE_LIMIT; ++i) {
    if (tables[i].entries) continue;
    tables[i].entries = table;
    tables[i].count = count;
    return TRUE;
  }
  ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
  return FALSE;
}

static char fold(char c) {
  return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

/* Integer IDs, "#123" strings and case-insensitive names all match. */
static BOOL name_id(LPCSTR name, uintptr_t *id) {
  if (IS_INTRESOURCE(name)) {
    *id = (uintptr_t)name;
    return TRUE;
  }
  if (name[0] != '#' || !name[1]) return FALSE;
  uintptr_t value = 0;
  for (const char *p = name + 1; *p; ++p) {
    if (*p < '0' || *p > '9' || value > 0xFFFFu) return FALSE;
    value = value * 10u + (uintptr_t)(*p - '0');
  }
  *id = value;
  return value <= 0xFFFFu;
}

static BOOL same_name(LPCSTR a, LPCSTR b) {
  uintptr_t ia, ib;
  const BOOL na = name_id(a, &ia), nb = name_id(b, &ib);
  if (na || nb) return na && nb && ia == ib;
  for (; *a && *b; ++a, ++b)
    if (fold(*a) != fold(*b)) return FALSE;
  return *a == *b;
}

HRSRC FindResource(HINSTANCE instance, LPCSTR name, LPCSTR type) {
  (void)instance;
  if (!name || !type) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  BOOL typeSeen = FALSE;
  for (UINT t = 0; t < UI_RESOURCE_TABLE_LIMIT; ++t) {
    for (UINT i = 0; tables[t].entries && i < tables[t].count; ++i) {
      const UNDOS_RESOURCE *entry = &tables[t].entries[i];
      if (!entry->type || !entry->name || !same_name(entry->type, type))
        continue;
      typeSeen = TRUE;
      if (same_name(entry->name, name)) return (HRSRC)entry;
    }
  }
  ui_core_failure(UI_STATUS_NOT_FOUND, typeSeen ? ERROR_RESOURCE_NAME_NOT_FOUND
                                                : ERROR_RESOURCE_TYPE_NOT_FOUND);
  return NULL;
}

HGLOBAL LoadResource(HINSTANCE instance, HRSRC resource) {
  (void)instance;
  if (!resource) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return NULL;
  }
  return (HGLOBAL)((const UNDOS_RESOURCE *)resource)->data;
}

LPVOID LockResource(HGLOBAL data) {
  return data;
}

DWORD SizeofResource(HINSTANCE instance, HRSRC resource) {
  (void)instance;
  if (!resource) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return 0;
  }
  return ((const UNDOS_RESOURCE *)resource)->size;
}

HBITMAP LoadBitmap(HINSTANCE instance, LPCSTR name) {
  const UNDOS_RESOURCE *entry =
      (const UNDOS_RESOURCE *)FindResource(instance, name, RT_BITMAP);
  if (!entry) return NULL;
  const uint8_t *data = (const uint8_t *)entry->data;
  DWORD size = entry->size;
  if (data && size >= 2 && data[0] == 'B' && data[1] == 'M') {
    if (size < UI_BITMAP_FILE_HEADER_BYTES) data = NULL;
    else {
      data += UI_BITMAP_FILE_HEADER_BYTES;
      size -= UI_BITMAP_FILE_HEADER_BYTES;
    }
  }
  BITMAPINFOHEADER header;
  if (!data || size < sizeof(header)) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  memcpy(&header, data, sizeof(header));
  if (header.biSize < sizeof(header) || header.biSize > size) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  DWORD colors = header.biClrUsed;
  if (!colors && header.biBitCount <= 8) colors = 1u << header.biBitCount;
  const uint64_t offset = (uint64_t)header.biSize + (uint64_t)colors * 4u;
  const LONG height = header.biHeight < 0 ? -header.biHeight : header.biHeight;
  const uint64_t stride =
      (((uint64_t)(header.biWidth > 0 ? header.biWidth : 0) * header.biBitCount +
        31u) / 32u) * 4u;
  if (offset + stride * (uint64_t)height > size) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  return ui_bitmap_from_dib((const BITMAPINFO *)data, data + offset);
}

int LoadString(HINSTANCE instance, UINT id, LPSTR buffer, int size) {
  if (!buffer || size <= 0) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  buffer[0] = '\0';
  if (id > 0xFFFFu) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return 0;
  }
  const UNDOS_RESOURCE *entry = (const UNDOS_RESOURCE *)FindResource(
      instance, MAKEINTRESOURCE(id), RT_STRING);
  if (!entry || !entry->data) return 0;
  const char *text = (const char *)entry->data;
  size_t length = strlen(text);
  if (length > (size_t)size - 1u) length = (size_t)size - 1u;
  memcpy(buffer, text, length);
  buffer[length] = '\0';
  return (int)length;
}

HACCEL LoadAccelerators(HINSTANCE instance, LPCSTR name) {
  const UNDOS_RESOURCE *entry =
      (const UNDOS_RESOURCE *)FindResource(instance, name, RT_ACCELERATOR);
  if (!entry) return NULL;
  const int count = (int)(entry->size / sizeof(ACCEL));
  if (!entry->data || count <= 0) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_PARAMETER);
    return NULL;
  }
  return CreateAcceleratorTable((LPACCEL)entry->data, count);
}
