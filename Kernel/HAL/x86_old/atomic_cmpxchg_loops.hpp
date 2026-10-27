#pragma once

#ifdef UNDOS_ATOMIC_CMPXCHG_TEST_HOOKS
void atomic_cmpxchg_test_before_attempt(volatile unsigned int *target) noexcept;
void atomic_cmpxchg_test_after_attempt(volatile unsigned int *target,
                                       unsigned int returned,
                                       unsigned int expected) noexcept;
#endif

unsigned int atomic_fetch_add_cmpxchg(volatile void *ptr,
                                      unsigned int val,
                                      PlatformMemoryOrder order) noexcept {
  volatile auto *p = static_cast<volatile unsigned int *>(ptr);
  atomic_memory_order(order);
  unsigned int expected, old, newval;
  do {
    expected = *p;
    old = expected;
    newval = expected + val;
#ifdef UNDOS_ATOMIC_CMPXCHG_TEST_HOOKS
    atomic_cmpxchg_test_before_attempt(p);
#endif
    asm volatile(
        "lock cmpxchg %2, %1"
        : "+a"(old), "+m"(*p)
        : "r"(newval)
        : "cc", "memory");
#ifdef UNDOS_ATOMIC_CMPXCHG_TEST_HOOKS
    atomic_cmpxchg_test_after_attempt(p, old, expected);
#endif
  } while (old != expected);
  atomic_memory_order(order);
  return expected;
}

unsigned int atomic_fetch_sub_cmpxchg(volatile void *ptr,
                                      unsigned int val,
                                      PlatformMemoryOrder order) noexcept {
  volatile auto *p = static_cast<volatile unsigned int *>(ptr);
  atomic_memory_order(order);
  unsigned int expected, old, newval;
  do {
    expected = *p;
    old = expected;
    newval = expected - val;
#ifdef UNDOS_ATOMIC_CMPXCHG_TEST_HOOKS
    atomic_cmpxchg_test_before_attempt(p);
#endif
    asm volatile(
        "lock cmpxchg %2, %1"
        : "+a"(old), "+m"(*p)
        : "r"(newval)
        : "cc", "memory");
#ifdef UNDOS_ATOMIC_CMPXCHG_TEST_HOOKS
    atomic_cmpxchg_test_after_attempt(p, old, expected);
#endif
  } while (old != expected);
  atomic_memory_order(order);
  return expected;
}
