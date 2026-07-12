
#include "MemModelToPlatformEnum.hpp"

extern "C" unsigned int __atomic_fetch_add_4(volatile void *ptr,
                                             unsigned int val,
                                             int memmodel) {
#ifdef NO_XADD
#else
  (void) memmodel;

  unsigned int old = val;
  __asm__ __volatile__(
      "lock xadd %0, %1"
      : "+r"(old), "+m"(*(unsigned int *) ptr)
      :
      : "memory");
  return old;
#endif
}
