
#include <Kernel.hpp>

#include "atomics.hpp"
#include "MemModelToPlatformEnum.hpp"

using atomic_add_fn = unsigned int (*)(volatile void *ptr, unsigned int val, PlatformMemoryOrder memmodel);
using atomic_sub_fn = unsigned int (*)(volatile void *ptr, unsigned int val, PlatformMemoryOrder memmodel);
using atomic_cmpxchg32_fn = bool (*)(volatile void *ptr, uint32_t &expected, uint32_t desired, PlatformMemoryOrder memmodel);
using atomic_load64_fn = uint64_t (*)(volatile uint64_t *p);
using atomic_store64_fn = void (*)(volatile uint64_t *p, uint64_t val);
using atomic_exchange64_fn = uint64_t (*)(volatile uint64_t *p, uint64_t val);
using atomic_cmpxchg64_fn = bool (*)(volatile uint64_t *p, uint64_t &expected, uint64_t desired);

namespace {
volatile unsigned int atomic_lock_word = 0;

void atomic_lock_386() noexcept {
  // 386-safe spinlock using XCHG (the only atomic instruction guaranteed)
  while (true) {
    unsigned int expected = 0;
    asm volatile(
        "xchg %0, %1"
        : "+r"(expected), "+m"(atomic_lock_word)
        :
        : "memory");
    if (expected == 0)
      break;
  }
}

void atomic_unlock_386() noexcept { atomic_lock_word = 0; }

void atomic_memory_order(PlatformMemoryOrder order) noexcept {
  switch (order) {
    case PlatformMemoryOrder::Relaxed:
    case PlatformMemoryOrder::Consume:
      // No barrier needed
      break;

    case PlatformMemoryOrder::Acquire:
    case PlatformMemoryOrder::Release:
    case PlatformMemoryOrder::AcquireRelease:
      asm volatile("" ::: "memory");
      break;

    case PlatformMemoryOrder::SequentiallyConsistent: {
      unsigned int dummy = 0;
      asm volatile("lock addl $0, %0" : "+m"(dummy)::"memory");
    } break;
  }
}

unsigned int atomic_fetch_add_386(volatile void *ptr,
                                  unsigned int val,
                                  PlatformMemoryOrder order) noexcept {
  volatile auto *p = static_cast<volatile unsigned int *>(ptr);

  // Release semantics before lock acquisition
  if (order == PlatformMemoryOrder::Release ||
      order == PlatformMemoryOrder::AcquireRelease)
    asm volatile("" ::: "memory");

  atomic_lock_386();

  unsigned int old = *p;
  *p = old + val;

  atomic_unlock_386();

  // Acquire semantics after unlock
  if (order == PlatformMemoryOrder::Acquire ||
      order == PlatformMemoryOrder::AcquireRelease)
    asm volatile("" ::: "memory");

  if (order == PlatformMemoryOrder::SequentiallyConsistent) {
    unsigned int dummy = 0;
    asm volatile("lock addl $0, %0" : "+m"(dummy)::"memory");
  }
  return old;
}

unsigned int atomic_fetch_sub_386(volatile void *ptr,
                                  unsigned int val,
                                  PlatformMemoryOrder order) noexcept {
  volatile auto *p = static_cast<volatile unsigned int *>(ptr);

  if (order == PlatformMemoryOrder::Release ||
      order == PlatformMemoryOrder::AcquireRelease)
    asm volatile("" ::: "memory");

  atomic_lock_386();

  unsigned int old = *p;
  *p = old - val;

  atomic_unlock_386();

  if (order == PlatformMemoryOrder::Acquire ||
      order == PlatformMemoryOrder::AcquireRelease)
    asm volatile("" ::: "memory");

  if (order == PlatformMemoryOrder::SequentiallyConsistent) {
    unsigned int dummy = 0;
    asm volatile("lock addl $0, %0" : "+m"(dummy)::"memory");
  }

  return old;
}

unsigned int atomic_fetch_add_xadd(volatile void *ptr,
                                   unsigned int val,
                                   PlatformMemoryOrder order) noexcept {
  volatile auto *p = static_cast<volatile unsigned int *>(ptr);
  atomic_memory_order(order);
  asm volatile(
      "lock xadd %0, %1"
      : "+r"(val), "+m"(*p)
      :
      : "memory");
  atomic_memory_order(order);
  return val;// xadd returns old value in the register
}

unsigned int atomic_fetch_sub_xadd(volatile void *ptr,
                                   unsigned int val,
                                   PlatformMemoryOrder order) noexcept {
  volatile auto *p = static_cast<volatile unsigned int *>(ptr);
  atomic_memory_order(order);
  val = -val;// xadd only adds, so negate
  asm volatile(
      "lock xadd %0, %1"
      : "+r"(val), "+m"(*p)
      :
      : "memory");
  atomic_memory_order(order);
  return val;// old value
}

unsigned int atomic_fetch_add_cmpxchg(volatile void *ptr,
                                      unsigned int val,
                                      PlatformMemoryOrder order) noexcept {
  volatile auto *p = static_cast<volatile unsigned int *>(ptr);
  atomic_memory_order(order);
  unsigned int old, newval;
  do {
    old = *p;
    newval = old + val;

    asm volatile(
        "lock cmpxchg %2, %1"
        : "+a"(old), "+m"(*p)
        : "r"(newval)
        : "memory");
    // cmpxchg sets ZF=1 on success, but we don't need ZF explicitly:
    // old == *p means success
  } while (old != *p);
  atomic_memory_order(order);
  return old;
}

unsigned int atomic_fetch_sub_cmpxchg(volatile void *ptr,
                                      unsigned int val,
                                      PlatformMemoryOrder order) noexcept {
  volatile auto *p = static_cast<volatile unsigned int *>(ptr);
  atomic_memory_order(order);
  unsigned int old, newval;
  do {
    old = *p;
    newval = old - val;

    asm volatile(
        "lock cmpxchg %2, %1"
        : "+a"(old), "+m"(*p)
        : "r"(newval)
        : "memory");
  } while (old != *p);
  atomic_memory_order(order);
  return old;
}

bool atomic_compare_exchange_32_386(volatile void *ptr,
                                    uint32_t &expected,
                                    uint32_t desired,
                                    PlatformMemoryOrder order) noexcept {
  // CMPXCHG isn't guaranteed on plain 386, fall back to the spinlock.
  volatile auto *p = static_cast<volatile uint32_t *>(ptr);

  if (order == PlatformMemoryOrder::Release ||
      order == PlatformMemoryOrder::AcquireRelease)
    asm volatile("" ::: "memory");

  atomic_lock_386();

  bool success = (*p == expected);
  if (success) {
    *p = desired;
  } else {
    expected = *p;
  }

  atomic_unlock_386();

  if (order == PlatformMemoryOrder::Acquire ||
      order == PlatformMemoryOrder::AcquireRelease)
    asm volatile("" ::: "memory");

  if (order == PlatformMemoryOrder::SequentiallyConsistent) {
    unsigned int dummy = 0;
    asm volatile("lock addl $0, %0" : "+m"(dummy)::"memory");
  }

  return success;
}

bool atomic_compare_exchange_32_cmpxchg(volatile void *ptr,
                                        uint32_t &expected,
                                        uint32_t desired,
                                        PlatformMemoryOrder order) noexcept {
  volatile auto *p = static_cast<volatile uint32_t *>(ptr);
  atomic_memory_order(order);
  uint32_t old = expected;
  asm volatile(
      "lock cmpxchg %2, %1"
      : "+a"(old), "+m"(*p)
      : "r"(desired)
      : "memory");
  atomic_memory_order(order);
  const bool success = old == expected;
  if (!success) {
    expected = old;
  }
  return success;
}

uint64_t atomic_load_64_386(volatile uint64_t *p) noexcept {
  atomic_lock_386();
  const uint64_t val = *p;
  atomic_unlock_386();
  return val;
}

void atomic_store_64_386(volatile uint64_t *p, uint64_t val) noexcept {
  atomic_lock_386();
  *p = val;
  atomic_unlock_386();
}

uint64_t atomic_exchange_64_386(volatile uint64_t *p, uint64_t val) noexcept {
  atomic_lock_386();
  const uint64_t old = *p;
  *p = val;
  atomic_unlock_386();
  return old;
}

bool atomic_compare_exchange_64_386(volatile uint64_t *p, uint64_t &expected, uint64_t desired) noexcept {
  atomic_lock_386();
  const bool success = (*p == expected);
  if (success)
    *p = desired;
  else
    expected = *p;
  atomic_unlock_386();
  return success;
}

// Pentium+ (CPUID CX8 feature bit) native 64-bit atomics using CMPXCHG8B.
bool atomic_compare_exchange_64_cmpxchg8b(volatile uint64_t *p, uint64_t &expected, uint64_t desired) noexcept {
  uint32_t desired_lo = static_cast<uint32_t>(desired);
  uint32_t desired_hi = static_cast<uint32_t>(desired >> 32);
  uint64_t old = expected;
  bool success;
  asm volatile(
      "lock cmpxchg8b %1\n\t"
      "sete %0"
      : "=q"(success), "+m"(*p), "+A"(old)
      : "b"(desired_lo), "c"(desired_hi)
      : "cc", "memory");
  if (!success)
    expected = old;
  return success;
}

uint64_t atomic_load_64_cmpxchg8b(volatile uint64_t *p) noexcept {
  // A CMPXCHG8B where EDX:EAX is seeded from EBX:ECX will (almost certainly)
  // fail the compare and load the current value into EDX:EAX without ever
  // modifying memory, giving an atomic 64-bit read on CPUs lacking a plain
  // atomic 64-bit load.
  uint64_t r;
  asm volatile(
      "movl %%ebx, %%eax\n\t"
      "movl %%ecx, %%edx\n\t"
      "lock cmpxchg8b %1"
      : "=&A"(r)
      : "m"(*p)
      : "cc");
  return r;
}

void atomic_store_64_cmpxchg8b(volatile uint64_t *p, uint64_t val) noexcept {
  uint64_t expected = atomic_load_64_cmpxchg8b(p);
  while (!atomic_compare_exchange_64_cmpxchg8b(p, expected, val)) {
    // expected is refreshed with the current value on failure, retry.
  }
}

uint64_t atomic_exchange_64_cmpxchg8b(volatile uint64_t *p, uint64_t val) noexcept {
  uint64_t old = atomic_load_64_cmpxchg8b(p);
  while (!atomic_compare_exchange_64_cmpxchg8b(p, old, val)) {
    // old is refreshed with the current value on failure, retry.
  }
  return old;
}

atomic_add_fn atomic_fetch_add_impl = &atomic_fetch_add_386;
atomic_sub_fn atomic_fetch_sub_impl = &atomic_fetch_sub_386;
atomic_cmpxchg32_fn atomic_compare_exchange_32_impl = &atomic_compare_exchange_32_386;
atomic_load64_fn atomic_load_64_impl = &atomic_load_64_386;
atomic_store64_fn atomic_store_64_impl = &atomic_store_64_386;
atomic_exchange64_fn atomic_exchange_64_impl = &atomic_exchange_64_386;
atomic_cmpxchg64_fn atomic_compare_exchange_64_impl = &atomic_compare_exchange_64_386;
}// namespace

namespace hal::x86 {
void init_atomic_ops(AtomicPlatorm platform) {
  switch (platform) {
    case AtomicPlatorm::X86_386:
      KE_Print("Using 386 atomics");
      break;
    case AtomicPlatorm::X86_XADD:
      KE_Print("Using XADD atomics");
      atomic_fetch_add_impl = &atomic_fetch_add_xadd;
      atomic_fetch_sub_impl = &atomic_fetch_sub_xadd;
      // XADD implies 486+, which also guarantees CMPXCHG.
      atomic_compare_exchange_32_impl = &atomic_compare_exchange_32_cmpxchg;
      break;
    case AtomicPlatorm::X86_CMPXCHG:
      KE_Print("Using CMPXCHG atomics");
      atomic_fetch_add_impl = &atomic_fetch_add_cmpxchg;
      atomic_fetch_sub_impl = &atomic_fetch_sub_cmpxchg;
      atomic_compare_exchange_32_impl = &atomic_compare_exchange_32_cmpxchg;
      break;
    case AtomicPlatorm::X86_CMPXCHG8B:
      KE_Print("Using CMPXCHG8B atomics");
      atomic_fetch_add_impl = &atomic_fetch_add_cmpxchg;
      atomic_fetch_sub_impl = &atomic_fetch_sub_cmpxchg;
      atomic_compare_exchange_32_impl = &atomic_compare_exchange_32_cmpxchg;
      // CMPXCHG8B (Pentium+) also gives us native 64-bit atomics.
      atomic_load_64_impl = &atomic_load_64_cmpxchg8b;
      atomic_store_64_impl = &atomic_store_64_cmpxchg8b;
      atomic_exchange_64_impl = &atomic_exchange_64_cmpxchg8b;
      atomic_compare_exchange_64_impl = &atomic_compare_exchange_64_cmpxchg8b;
      break;
  }
}
}// namespace hal::x86

extern "C" unsigned int __atomic_fetch_add_4(volatile void *ptr, unsigned int val, int memmodel) {
  return atomic_fetch_add_impl(ptr, val, to_memory_order(memmodel));
}

extern "C" unsigned int __atomic_fetch_sub_4(volatile void *ptr, unsigned int val, int memmodel) {
  return atomic_fetch_sub_impl(ptr, val, to_memory_order(memmodel));
}

UNDOS_HAL_API_DEF void HAL_ATOMIC_ThreadFence(PlatformMemoryOrder order) noexcept {
  atomic_memory_order(order);
}

UNDOS_HAL_API_DEF uint32_t HAL_ATOMIC_Load32(kernel::VirtualAddress addr, PlatformMemoryOrder order) noexcept {
  const volatile auto *p = addr.as_ptr<volatile uint32_t>();
  atomic_memory_order(order);
  const uint32_t val = *p;
  atomic_memory_order(order);
  return val;
}

UNDOS_HAL_API_DEF void HAL_ATOMIC_Store32(kernel::VirtualAddress addr, uint32_t val, PlatformMemoryOrder order) noexcept {
  volatile auto *p = addr.as_ptr<volatile uint32_t>();
  atomic_memory_order(order);
  *p = val;
  atomic_memory_order(order);
}

UNDOS_HAL_API_DEF uint32_t HAL_ATOMIC_Exchange32(kernel::VirtualAddress addr, uint32_t val, PlatformMemoryOrder order) noexcept {
  volatile auto *p = addr.as_ptr<volatile uint32_t>();
  atomic_memory_order(order);
  asm volatile(
      "xchg %0, %1"
      : "+r"(val), "+m"(*p)
      :
      : "memory");
  atomic_memory_order(order);
  return val;// xchg returns old value in the register
}

UNDOS_HAL_API_DEF bool HAL_ATOMIC_CompareExchange32(kernel::VirtualAddress addr, uint32_t &expected_out, uint32_t desired, PlatformMemoryOrder order) noexcept {
  return atomic_compare_exchange_32_impl(addr.as_ptr<void>(), expected_out, desired, order);
}

UNDOS_HAL_API_DEF uint32_t HAL_ATOMIC_FetchAdd32(kernel::VirtualAddress addr, uint32_t delta, PlatformMemoryOrder order) noexcept {
  return atomic_fetch_add_impl(addr.as_ptr<void>(), delta, order);
}

UNDOS_HAL_API_DEF uint64_t HAL_ATOMIC_Load64(kernel::VirtualAddress addr, PlatformMemoryOrder order) noexcept {
  // Uses CMPXCHG8B on Pentium+ (see init_atomic_ops); falls back to the
  // 386-safe spinlock otherwise, since no x86 tier we support guarantees a
  // plain atomic 64-bit access.
  volatile auto *p = addr.as_ptr<volatile uint64_t>();
  atomic_memory_order(order);
  const uint64_t val = atomic_load_64_impl(p);
  atomic_memory_order(order);
  return val;
}

UNDOS_HAL_API_DEF void HAL_ATOMIC_Store64(kernel::VirtualAddress addr, uint64_t val, PlatformMemoryOrder order) noexcept {
  volatile auto *p = addr.as_ptr<volatile uint64_t>();
  atomic_memory_order(order);
  atomic_store_64_impl(p, val);
  atomic_memory_order(order);
}

UNDOS_HAL_API_DEF uint64_t HAL_ATOMIC_Exchange64(kernel::VirtualAddress addr, uint64_t val, PlatformMemoryOrder order) noexcept {
  volatile auto *p = addr.as_ptr<volatile uint64_t>();
  atomic_memory_order(order);
  const uint64_t old = atomic_exchange_64_impl(p, val);
  atomic_memory_order(order);
  return old;
}

UNDOS_HAL_API_DEF bool HAL_ATOMIC_CompareExchange64(kernel::VirtualAddress addr, uint64_t &expected_out, uint64_t desired, PlatformMemoryOrder order) noexcept {
  volatile auto *p = addr.as_ptr<volatile uint64_t>();
  atomic_memory_order(order);
  const bool success = atomic_compare_exchange_64_impl(p, expected_out, desired);
  atomic_memory_order(order);
  return success;
}
