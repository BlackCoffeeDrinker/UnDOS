#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include <undoscrt/process.h>
#include <undos/syscalls.h>

extern "C" int main(int argc, char **argv) {
  (void) argc;
  (void) argv;

  crt_printf("\n=================================================\n");
  crt_printf("  UnDOS Interactive Console Shell (/System/init.elf)\n");
  crt_printf("  Virtual TTY subsystem active on stdio (handles 0, 1)\n");
  crt_printf("=================================================\n\n");

  char line[128];
  for (int prompt = 1; prompt <= 3; ++prompt) {
    crt_printf("undos:%d> ", prompt);
    if (crt_readline(line, sizeof(line))) {
      crt_printf("Executed: %s", line);
    } else {
      break;
    }
  }

  crt_printf("init: interactive session finished.\n");
  return 0;
}
