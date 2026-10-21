#include "../../CRT/libc/backend.h"
#include <errno.h>
#include <string.h>
#include <undoscrt/stdio.h>
#include <stdbool.h>

static _Alignas(16) unsigned char arena[128 * 1024];
static size_t allocated;
static const char *input = "ab\nZ";
static size_t position;
static char output[16384];
static size_t written;
static bool available = true;
static int io_error;
static size_t write_limit = 2;
static size_t fail_after = sizeof(output);

_Noreturn void test_terminate(int code);
int crt_initialize_stdio(void) { return 0; }
int crt_get_std_handle(enum StdHandleSelector selector, HANDLE *handle) {
  if (!available) return -1;
  *handle = 37 + (unsigned)selector;
  return 0;
}
void test_io(int error, int present, size_t limit) {
  io_error = error;
  available = present;
  write_limit = limit;
}
const char *test_output(void) { output[written] = '\0'; return output; }
void test_clear_output(void) { written = 0; }
void test_fail_after(size_t count) { fail_after = count; }
void test_input(const char *text) { input = text; position = 0; }
_Noreturn void undos_libc_terminate(int code) { test_terminate(code); }
void undos_libc_startup_error(void) { __builtin_trap(); }
void *undos_libc_allocate_pages(size_t size) {
  if (size > sizeof(arena) - allocated) return NULL;
  void *result = arena + allocated;
  allocated += size;
  return result;
}
int undos_libc_read(HANDLE handle, void *buffer, size_t *size) {
  if (*size > UNDOS_LIBC_IO_CHUNK_SIZE) return EINVAL;
  if (io_error) { *size = 0; return io_error; }
  if (handle != 37 && handle != 900) return EBADF;
  size_t done = 0;
  while (done < *size && input[position]) {
    ((char *)buffer)[done++] = input[position++];
  }
  *size = done;
  return 0;
}
int undos_libc_write(HANDLE handle, const void *buffer, size_t *size) {
  if (*size > UNDOS_LIBC_IO_CHUNK_SIZE) return EINVAL;
  if (io_error) { *size = 0; return io_error; }
  if (handle != 38 && handle != 39) return EBADF;
  if (written >= fail_after) { *size = 0; return EIO; }
  if (*size > fail_after - written) *size = fail_after - written;
  if (*size > write_limit) *size = write_limit;
  if (*size > sizeof(output) - 1 - written) { *size = 0; return EIO; }
  memcpy(output + written, buffer, *size);
  written += *size;
  return 0;
}
int undos_libc_open(const char *path, HANDLE *handle) {
  if (strcmp(path, "/input")) return EIO;
  position = 0;
  *handle = 900;
  return 0;
}
int undos_libc_close(HANDLE handle) { (void)handle; return io_error; }
