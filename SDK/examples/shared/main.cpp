#include <undoscrt/stdio.h>

extern "C" int message_value();

int main() {
  if (message_value() != 73) return 1;
  return crt_puts("Hello from an UnDOS shared library!\n") < 0 ? 1 : 0;
}
