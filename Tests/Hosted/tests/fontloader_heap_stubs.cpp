// Host stand-ins for the userland CRT heap used by Apps/Fontloader.
#include <cstdlib>
#include <undoscrt/heap.h>

extern "C" void *crt_realloc(void *ptr, size_t size) { return std::realloc(ptr, size); }
extern "C" void crt_free(void *ptr) { std::free(ptr); }

[[noreturn]] void fontloader_allocation_failure() { std::abort(); }
