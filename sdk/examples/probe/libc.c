#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <limits.h>
#include <errno.h>
#include <userspace.h>

_Static_assert(sizeof(int) == 4 && sizeof(long) == 4 && sizeof(void *) == 4, "ILP32");
_Static_assert(sizeof(int64_t) == 8 && CHAR_BIT == 8 && SIZE_MAX == UINT32_MAX, "C types");
_Static_assert(sizeof(HANDLE) == 4 && _Alignof(HANDLE) == 4, "handle ABI");
_Static_assert(sizeof(struct __SYS_PROC_CREATEPROCESS_V2) == 36 &&
               offsetof(struct __SYS_PROC_CREATEPROCESS_V2, stdinHandle) == 20 &&
               offsetof(struct __SYS_PROC_CREATEPROCESS_V2, stdoutHandle) == 24 &&
               offsetof(struct __SYS_PROC_CREATEPROCESS_V2, stderrHandle) == 28 &&
               offsetof(struct __SYS_PROC_CREATEPROCESS_V2, handle) == 32,
               "CreateProcessV2 wire ABI");

static int failed(int line) {
  char message[64];
  snprintf(message, sizeof(message), "SDK probe: libc FAIL at line %d", line);
  (void) UD_SYS_PrintV1(message);
  return 0;
}
#define FAIL() return failed(__LINE__)

int sdk_libc_from_c(void) {
  char text[80];
  if (snprintf(text, sizeof(text), "%s %d %#x %zu", "C", INT_MIN, 42u, (size_t)73) != 21 ||
      strcmp(text, "C -2147483648 0x2a 73")) { FAIL(); }
  char *memory = calloc(4, 16);
  if (!memory || (uintptr_t)memory % 16 || memory[63]) { FAIL(); }
  strcpy(memory, "standard C");
  char *grown = realloc(memory, 128);
  if (!grown || strcmp(grown, "standard C")) { FAIL(); }
  free(grown);
  if (malloc(SIZE_MAX) || errno != ENOMEM) { FAIL(); }
  FILE *input = fopen("/System/libcin.txt", "r");
  if (!input || !fgets(text, sizeof(text), input) || strcmp(text, "input 73\n") ||
      fgetc(input) != EOF || !feof(input) || ferror(input) || fclose(input)) { FAIL(); }

  HANDLE handle = 0;
  uint32_t transferred = 99;
  if (UD_SYS_WriteV2(UINT32_MAX, (const uint8_t *)"x", 1, &transferred) ==
      SyscallReturnCode_OKAY || transferred != 0) { FAIL(); }
  if (UD_SYS_OpenV1("/System/libcin.txt", FileOpenMode_Read, &handle) !=
          SyscallReturnCode_OKAY ||
      UD_SYS_WriteV2(handle, (const uint8_t *)"x", 1, &transferred) !=
          SyscallReturnCode_UNKNOWN || transferred != 0 ||
      UD_SYS_CloseV1(handle) != SyscallReturnCode_OKAY) { FAIL(); }
  HANDLE source = 0, sink = 0;
  if (UD_SYS_CreateV1(&source, &sink) != SyscallReturnCode_OKAY ||
      UD_SYS_WriteV2(sink, NULL, 0, &transferred) != SyscallReturnCode_OKAY ||
      transferred != 0 ||
      UD_SYS_WriteV2(sink, (const uint8_t *)"short write", 11, &transferred) !=
          SyscallReturnCode_OKAY || transferred != 11) { FAIL(); }
  uint8_t bulk[4096];
  memset(bulk, 'B', sizeof(bulk));
  if (UD_SYS_WriteV2(sink, bulk, sizeof(bulk) + 1, &transferred) !=
          SyscallReturnCode_INVALID_SIZE ||
      UD_SYS_WriteV2(sink, bulk, sizeof(bulk), &transferred) != SyscallReturnCode_OKAY ||
      transferred != sizeof(bulk) - 11 ||
      UD_SYS_WriteV2(sink, bulk, 1, &transferred) != SyscallReturnCode_OKAY ||
      transferred != 0) { FAIL(); }
  uint32_t length = sizeof(bulk);
  if (UD_SYS_ReadV1(source, bulk, &length) != SyscallReturnCode_OKAY ||
      length != sizeof(bulk) || memcmp(bulk, "short write", 11) ||
      UD_SYS_CloseV1(source) != SyscallReturnCode_OKAY ||
      UD_SYS_CloseV1(sink) != SyscallReturnCode_OKAY) { FAIL(); }
  if (UD_SYS_GetStdHandleV1(StdHandleSelector_Stdout, &handle) != SyscallReturnCode_OKAY) {
    if (puts("unavailable") != EOF || !ferror(stdout) || errno != EBADF) { FAIL(); }
    clearerr(stdout);
    (void) UD_SYS_PrintV1("SDK probe: standard C output/input/errors PASS");
    return 1;
  }
  if (UD_SYS_WriteV2(handle, NULL, 0, &transferred) != SyscallReturnCode_OKAY ||
      transferred != 0) { FAIL(); }
  if (setvbuf(stdout, NULL, _IOLBF, BUFSIZ) ||
      puts("Hello from UnDOS!") == EOF || printf("C stdio: %d\n", 73) != 12 ||
      fflush(stdout)) { FAIL(); }
  if (UD_SYS_CloseV1(handle) != SyscallReturnCode_OKAY ||
      UD_SYS_GetStdHandleV1(StdHandleSelector_Stdout, &handle) == SyscallReturnCode_OKAY ||
      puts("unavailable") != EOF || !ferror(stdout) || errno != EIO)
    { FAIL(); }
  clearerr(stdout);
  if (ferror(stdout)) { FAIL(); }
  (void) UD_SYS_PrintV1("SDK probe: standard C output/input/errors PASS");
  return 1;
}
