
#include <__fwd/panic.hpp>
#include <stdio.h>
#include <stdlib.h>

namespace kstd {
void panic(const char* msg) {
    fprintf(stderr, "PANIC: %s\n", msg);
    abort();
}
}
