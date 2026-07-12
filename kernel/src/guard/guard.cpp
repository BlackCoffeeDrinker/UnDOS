
#include <stdint.h>

extern "C" {
int __cxa_guard_acquire(uint64_t *guard) {
  return (*guard == 0);
}

void __cxa_guard_release(uint64_t *guard) {
  *guard = 1;
}

void __cxa_guard_abort(uint64_t *) {
  // nothing needed
}



}
