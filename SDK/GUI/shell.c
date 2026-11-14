#include <windows.h>
#include <undoscrt/string.h>

#include "private/backend.h"
#include "private/ui_internal.h"

/* ShellExecute and FindExecutable. .ELF files run directly; other files
   open with the program registered for their extension through
   UndosSetAssociation (the WIN.INI [Extensions] equivalent). */

#define UI_ASSOC_LIMIT 16
#define UI_ASSOC_EXTENSION 8
#define UI_SHELL_PATH 256
#define UI_SHELL_ARGS 1024

typedef struct Association {
  char extension[UI_ASSOC_EXTENSION];
  char program[UI_SHELL_PATH];
} Association;

static Association associations[UI_ASSOC_LIMIT];

static char upper(char c) { return c >= 'a' && c <= 'z' ? (char)(c - 32) : c; }

static BOOL same_text(const char *a, const char *b) {
  for (; *a && *b; ++a, ++b)
    if (upper(*a) != upper(*b)) return FALSE;
  return *a == *b;
}

static HINSTANCE shell_result(UINT_PTR code) {
  if (code <= 32) SetLastError((DWORD)code);
  return (HINSTANCE)code;
}

BOOL UndosSetAssociation(LPCSTR extension, LPCSTR program) {
  if (!extension || !extension[0] || extension[0] == '.' ||
      strlen(extension) >= UI_ASSOC_EXTENSION ||
      (program && strlen(program) >= UI_SHELL_PATH)) {
    SetLastError(ERROR_INVALID_PARAMETER);
    return FALSE;
  }
  Association *slot = NULL, *empty = NULL;
  for (size_t i = 0; i < UI_ASSOC_LIMIT; ++i) {
    if (!associations[i].extension[0]) {
      if (!empty) empty = &associations[i];
    } else if (same_text(associations[i].extension, extension)) {
      slot = &associations[i];
    }
  }
  if (!program) {
    if (slot) memset(slot, 0, sizeof(*slot));
    return TRUE;
  }
  if (!slot) slot = empty;
  if (!slot) {
    SetLastError(ERROR_NOT_ENOUGH_MEMORY);
    return FALSE;
  }
  memcpy(slot->extension, extension, strlen(extension) + 1);
  memcpy(slot->program, program, strlen(program) + 1);
  return TRUE;
}

static const char *extension_of(const char *file) {
  const char *dot = NULL;
  for (const char *p = file; *p; ++p) {
    if (*p == '.') dot = p;
    else if (*p == '/' || *p == '\\') dot = NULL;
  }
  return dot ? dot + 1 : "";
}

/* Builds an absolute UnDOS path from file and an optional directory. */
static BOOL resolve(LPCSTR file, LPCSTR directory, char *out) {
  const BOOL absolute = file[0] == '/' || file[0] == '\\' ||
                        (file[0] && file[1] == ':');
  size_t n = 0;
  const char *parts[2] = {absolute ? NULL : directory, file};
  for (size_t i = 0; i < 2; ++i) {
    const char *p = parts[i];
    if (!p) continue;
    if (p[0] && p[1] == ':') {
      if (upper(p[0]) != 'C') return FALSE;
      p += 2;
    }
    if (n && out[n - 1] != '/') out[n++] = '/';
    for (; *p; ++p) {
      if (n + 1 >= UI_SHELL_PATH) return FALSE;
      const char c = *p == '\\' ? '/' : *p;
      if (c == '/' && n && out[n - 1] == '/') continue;
      out[n++] = c;
    }
  }
  out[n] = 0;
  return n && out[0] == '/';
}

static UINT_PTR find_program(LPCSTR file, LPCSTR directory, char *path,
                             char *program) {
  if (!file || !file[0]) return SE_ERR_FNF;
  if (!resolve(file, directory, path)) return SE_ERR_PNF;
  const DWORD attributes = GetFileAttributes(path);
  if (attributes == INVALID_FILE_ATTRIBUTES)
    return GetLastError() == ERROR_PATH_NOT_FOUND ? SE_ERR_PNF : SE_ERR_FNF;
  if (attributes & FILE_ATTRIBUTE_DIRECTORY) return SE_ERR_NOASSOC;
  const char *extension = extension_of(path);
  if (same_text(extension, "ELF")) {
    memcpy(program, path, strlen(path) + 1);
    return 33;
  }
  for (size_t i = 0; i < UI_ASSOC_LIMIT; ++i) {
    if (associations[i].extension[0] &&
        same_text(associations[i].extension, extension)) {
      memcpy(program, associations[i].program,
             strlen(associations[i].program) + 1);
      return 33;
    }
  }
  return SE_ERR_NOASSOC;
}

HINSTANCE FindExecutable(LPCSTR file, LPCSTR directory, LPSTR result) {
  char path[UI_SHELL_PATH], program[UI_SHELL_PATH];
  if (result) result[0] = 0;
  const UINT_PTR code = find_program(file, directory, path, program);
  if (code > 32 && result) memcpy(result, program, strlen(program) + 1);
  return shell_result(code);
}

static BOOL append(char *buffer, size_t *length, const char *text, BOOL quote) {
  const size_t size = strlen(text);
  if (*length + size + 4 >= UI_SHELL_ARGS) return FALSE;
  if (*length) buffer[(*length)++] = ' ';
  if (quote) buffer[(*length)++] = '"';
  memcpy(buffer + *length, text, size);
  *length += size;
  if (quote) buffer[(*length)++] = '"';
  buffer[*length] = 0;
  return TRUE;
}

HINSTANCE ShellExecute(HWND window, LPCSTR operation, LPCSTR file,
                       LPCSTR parameters, LPCSTR directory, int show) {
  (void)window;
  (void)show;
  if (operation && operation[0] && !same_text(operation, "open"))
    return shell_result(SE_ERR_NOASSOC);
  char path[UI_SHELL_PATH], program[UI_SHELL_PATH];
  const UINT_PTR code = find_program(file, directory, path, program);
  if (code <= 32) return shell_result(code);
  char arguments[UI_SHELL_ARGS] = {0};
  size_t length = 0;
  const BOOL direct = strcmp(program, path) == 0;
  if (!direct && !append(arguments, &length, path, strchr(path, ' ') != NULL))
    return shell_result(SE_ERR_OOM);
  if (parameters && parameters[0] &&
      !append(arguments, &length, parameters, FALSE))
    return shell_result(SE_ERR_OOM);
  const uint32_t status = ui_backend_create_process(program, arguments);
  if (status == UI_STATUS_OK) return shell_result(33);
  return shell_result(status == UI_STATUS_NOT_FOUND ? SE_ERR_FNF
                      : status == UI_STATUS_OUT_OF_MEMORY ? SE_ERR_OOM
                                                          : SE_ERR_ACCESSDENIED);
}

void ui_shell_reset(void) { memset(associations, 0, sizeof(associations)); }
