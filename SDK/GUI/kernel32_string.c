#include <windows.h>
#include <stdarg.h>
#include <undoscrt/heap.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"

/* kernel32/user32 string helpers and the Local/Global/Heap allocators.
   TCHAR is char, so the A forms are the only forms. Moveable memory is
   never moved: a handle is the block's own address and Lock returns it. */

int vsnprintf(char *buffer, size_t size, const char *format, va_list args);

static char upper_char(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }
static char lower_char(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 32) : c; }

int lstrlen(LPCSTR text) { return text ? (int)strlen(text) : 0; }

LPSTR lstrcpy(LPSTR destination, LPCSTR source) {
  if (!destination || !source) return NULL;
  return strcpy(destination, source);
}

LPSTR lstrcpyn(LPSTR destination, LPCSTR source, int length) {
  if (!destination || !source || length <= 0) return NULL;
  int i = 0;
  for (; i < length - 1 && source[i]; ++i) destination[i] = source[i];
  destination[i] = 0;
  return destination;
}

LPSTR lstrcat(LPSTR destination, LPCSTR source) {
  if (!destination || !source) return NULL;
  return strcat(destination, source);
}

int lstrcmp(LPCSTR a, LPCSTR b) {
  if (!a || !b) return a ? 1 : (b ? -1 : 0);
  const int result = strcmp(a, b);
  return result < 0 ? -1 : result > 0;
}

int lstrcmpi(LPCSTR a, LPCSTR b) {
  if (!a || !b) return a ? 1 : (b ? -1 : 0);
  for (;; ++a, ++b) {
    const unsigned char x = (unsigned char)lower_char(*a);
    const unsigned char y = (unsigned char)lower_char(*b);
    if (x != y) return x < y ? -1 : 1;
    if (!x) return 0;
  }
}

/* Win32 caps wsprintf output at 1024 bytes including the terminator. */
int wvsprintf(LPSTR buffer, LPCSTR format, va_list args) {
  if (!buffer || !format) return -1;
  const int written = vsnprintf(buffer, 1024, format, args);
  if (written < 0) return -1;
  return written > 1023 ? 1023 : written;
}

int wsprintf(LPSTR buffer, LPCSTR format, ...) {
  va_list args;
  va_start(args, format);
  const int written = wvsprintf(buffer, format, args);
  va_end(args);
  return written;
}

/* A pointer whose high word is zero is a single character, as in Win32. */
LPSTR CharUpper(LPSTR text) {
  if (!((uintptr_t)text >> 16)) return (LPSTR)(uintptr_t)(unsigned char)upper_char((char)(uintptr_t)text);
  for (char *p = text; *p; ++p) *p = upper_char(*p);
  return text;
}

LPSTR CharLower(LPSTR text) {
  if (!((uintptr_t)text >> 16)) return (LPSTR)(uintptr_t)(unsigned char)lower_char((char)(uintptr_t)text);
  for (char *p = text; *p; ++p) *p = lower_char(*p);
  return text;
}

DWORD CharUpperBuff(LPSTR text, DWORD length) {
  if (!text) return 0;
  for (DWORD i = 0; i < length; ++i) text[i] = upper_char(text[i]);
  return length;
}

DWORD CharLowerBuff(LPSTR text, DWORD length) {
  if (!text) return 0;
  for (DWORD i = 0; i < length; ++i) text[i] = lower_char(text[i]);
  return length;
}

LPSTR CharNext(LPCSTR text) {
  if (!text) return NULL;
  return (LPSTR)(*text ? text + 1 : text);
}

LPSTR CharPrev(LPCSTR start, LPCSTR current) {
  if (!start || !current) return NULL;
  return (LPSTR)(current > start ? current - 1 : start);
}

BOOL IsCharAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
BOOL IsCharAlphaNumeric(char c) { return IsCharAlpha(c) || (c >= '0' && c <= '9'); }
BOOL IsCharUpper(char c) { return c >= 'A' && c <= 'Z'; }
BOOL IsCharLower(char c) { return c >= 'a' && c <= 'z'; }

/* Every block carries its size in front of the caller's bytes. */
typedef struct MemoryHeader {
  SIZE_T size;
  UINT flags;
  UINT magic;
} MemoryHeader;

#define UI_MEMORY_MAGIC 0x4D454D55u

static MemoryHeader *header_of(const void *memory) {
  if (!memory) return NULL;
  MemoryHeader *header = (MemoryHeader *)memory - 1;
  return header->magic == UI_MEMORY_MAGIC ? header : NULL;
}

static void *allocate(UINT flags, SIZE_T size, BOOL zero) {
  if (size > 0x7FFFFFF0u) {
    ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  MemoryHeader *header = (MemoryHeader *)crt_malloc(sizeof(MemoryHeader) + (size ? size : 1));
  if (!header) {
    ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  header->size = size;
  header->flags = flags;
  header->magic = UI_MEMORY_MAGIC;
  if (zero) memset(header + 1, 0, size);
  return header + 1;
}

static void *reallocate(void *memory, SIZE_T size, BOOL zero) {
  MemoryHeader *header = header_of(memory);
  if (!header) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return NULL;
  }
  const SIZE_T old = header->size;
  MemoryHeader *moved = (MemoryHeader *)crt_realloc(header, sizeof(MemoryHeader) + (size ? size : 1));
  if (!moved) {
    ui_core_failure(UI_STATUS_OUT_OF_MEMORY, ERROR_NOT_ENOUGH_MEMORY);
    return NULL;
  }
  moved->size = size;
  if (zero && size > old) memset((char *)(moved + 1) + old, 0, size - old);
  return moved + 1;
}

static void *release(void *memory) {
  if (!memory) return NULL;
  MemoryHeader *header = header_of(memory);
  if (!header) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return memory;
  }
  header->magic = 0;
  crt_free(header);
  return NULL;
}

static SIZE_T size_of(const void *memory) {
  const MemoryHeader *header = header_of(memory);
  if (!header) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return 0;
  }
  return header->size;
}

HLOCAL LocalAlloc(UINT flags, SIZE_T size) {
  return allocate(flags, size, (flags & LMEM_ZEROINIT) != 0);
}
HLOCAL LocalReAlloc(HLOCAL memory, SIZE_T size, UINT flags) {
  return reallocate(memory, size, (flags & LMEM_ZEROINIT) != 0);
}
HLOCAL LocalFree(HLOCAL memory) { return release(memory); }
LPVOID LocalLock(HLOCAL memory) { return header_of(memory) ? memory : NULL; }
BOOL LocalUnlock(HLOCAL memory) { (void)memory; return FALSE; }
SIZE_T LocalSize(HLOCAL memory) { return size_of(memory); }
HLOCAL LocalHandle(LPCVOID memory) { return header_of(memory) ? (HLOCAL)memory : NULL; }

HGLOBAL GlobalAlloc(UINT flags, SIZE_T size) {
  return allocate(flags, size, (flags & GMEM_ZEROINIT) != 0);
}
HGLOBAL GlobalReAlloc(HGLOBAL memory, SIZE_T size, UINT flags) {
  return reallocate(memory, size, (flags & GMEM_ZEROINIT) != 0);
}
HGLOBAL GlobalFree(HGLOBAL memory) { return release(memory); }
LPVOID GlobalLock(HGLOBAL memory) { return header_of(memory) ? memory : NULL; }
BOOL GlobalUnlock(HGLOBAL memory) { (void)memory; return FALSE; }
SIZE_T GlobalSize(HGLOBAL memory) { return size_of(memory); }
HGLOBAL GlobalHandle(LPCVOID memory) { return header_of(memory) ? (HGLOBAL)memory : NULL; }

/* One process heap; HeapCreate is not provided. */
#define UI_PROCESS_HEAP 1u

HANDLE GetProcessHeap(void) { return UI_PROCESS_HEAP; }

static BOOL valid_heap(HANDLE heap) {
  if (heap == UI_PROCESS_HEAP) return TRUE;
  ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
  return FALSE;
}

LPVOID HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size) {
  if (!valid_heap(heap)) return NULL;
  return allocate(0, size, (flags & HEAP_ZERO_MEMORY) != 0);
}
LPVOID HeapReAlloc(HANDLE heap, DWORD flags, LPVOID memory, SIZE_T size) {
  if (!valid_heap(heap)) return NULL;
  return reallocate(memory, size, (flags & HEAP_ZERO_MEMORY) != 0);
}
BOOL HeapFree(HANDLE heap, DWORD flags, LPVOID memory) {
  (void)flags;
  if (!valid_heap(heap)) return FALSE;
  return release(memory) == NULL;
}
SIZE_T HeapSize(HANDLE heap, DWORD flags, LPCVOID memory) {
  (void)flags;
  if (!valid_heap(heap)) return (SIZE_T)-1;
  const MemoryHeader *header = header_of(memory);
  if (!header) {
    ui_core_failure(UI_STATUS_INVALID_ARGUMENT, ERROR_INVALID_HANDLE);
    return (SIZE_T)-1;
  }
  return header->size;
}
