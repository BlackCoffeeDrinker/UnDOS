#include <stdio.h>
#include <string.h>
#include <userspace.h>

int main(int argc, char **argv) {
  HANDLE input, output, error, child;
  if (UD_SYS_GetStdHandleV1(StdHandleSelector_Stdin, &input) != SyscallReturnCode_OKAY ||
      UD_SYS_GetStdHandleV1(StdHandleSelector_Stdout, &output) != SyscallReturnCode_OKAY ||
      UD_SYS_GetStdHandleV1(StdHandleSelector_Stderr, &error) != SyscallReturnCode_OKAY)
    return 1;
  /* AccessDenied maps to UNKNOWN in the current syscall return-code ABI. */
  if (UD_SYS_CreateProcessV2(argv[0], "", 1, input, output, error, &child) !=
          SyscallReturnCode_INVALID_ARGUMENT ||
      UD_SYS_CreateProcessV2(argv[0], "", 0, UINT32_MAX, output, error, &child) !=
          SyscallReturnCode_INVALID_ARGUMENT ||
      UD_SYS_CreateProcessV2(argv[0], "", 0, output, output, error, &child) !=
          SyscallReturnCode_UNKNOWN ||
      UD_SYS_CreateProcessV2(argv[0], "", 0, input, input, error, &child) !=
          SyscallReturnCode_UNKNOWN ||
      UD_SYS_CreateProcessV2(argv[0], "", 0, input, output, input, &child) !=
          SyscallReturnCode_UNKNOWN) return 2;
  if (puts("Child stdout ready") == EOF ||
      fprintf(stderr, "Child stderr ready\n") < 0) return 3;
  (void) UD_SYS_PrintV1("Terminal probe: waiting for input");
  char line[80];
  if (argc == 2 && (!strcmp(argv[1], "--eof") || !strcmp(argv[1], "--close"))) {
    if (fgets(line, sizeof(line), stdin) || !feof(stdin) || ferror(stdin)) return 4;
    if (!strcmp(argv[1], "--close")) {
      (void) UD_SYS_PrintV1("Terminal probe: close PASS");
      return 0;
    }
    if (puts("Child EOF received") == EOF) return 5;
    (void) UD_SYS_PrintV1("Terminal probe: EOF PASS");
    return 0;
  }
  if (!fgets(line, sizeof(line), stdin) || strcmp(line, "hello child\n")) return 6;
  if (printf("Child received: %s", line) < 0) return 7;
  (void) UD_SYS_PrintV1("Terminal probe: streams PASS");
  return 73;
}
