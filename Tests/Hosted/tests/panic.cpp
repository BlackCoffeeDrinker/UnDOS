
#include <__fwd/panic.hpp>
#include <stdio.h>
#include <stdlib.h>

extern "C" [[noreturn]] void KE_RUNTIME_Panic(const char* msg) {
    fprintf(stderr, "PANIC: %s\n", msg);
    abort();
}
