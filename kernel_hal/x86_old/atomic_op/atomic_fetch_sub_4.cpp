
#include "MemModelToPlatformEnum.hpp"

extern "C" unsigned int __atomic_fetch_sub_4(volatile void *ptr,
                                             unsigned int val,
                                             int memmodel) {
#ifdef NO_XADD
#else
  (void) memmodel;

  auto neg = static_cast<unsigned int>(-static_cast<int>(val));// two's complement negate
  unsigned int old = neg;

  __asm__ __volatile__(
      "lock xadd %0, %1"
      : "+r"(old), "+m"(*(unsigned int *) ptr)
      :
      : "memory");

  return old;// old value before subtraction
#endif
}
