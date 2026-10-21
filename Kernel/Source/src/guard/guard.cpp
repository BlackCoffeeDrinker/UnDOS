
#include <stdint.h>
#include <__fwd/panic.hpp>

// Modules resolve these through the exported symbol registry; keep them out of -fvisibility=hidden.
#define UNDOS_RUNTIME_EXPORT __attribute__((visibility("default"), used))

extern "C" {
[[noreturn]] UNDOS_RUNTIME_EXPORT void __cxa_pure_virtual() {
  kstd::panic("Pure virtual function call");
}

UNDOS_RUNTIME_EXPORT int __cxa_guard_acquire(uint64_t *guard) {
  return (*guard == 0);
}

UNDOS_RUNTIME_EXPORT void __cxa_guard_release(uint64_t *guard) {
  *guard = 1;
}

UNDOS_RUNTIME_EXPORT void __cxa_guard_abort(uint64_t *) {
  // nothing needed
}



}
