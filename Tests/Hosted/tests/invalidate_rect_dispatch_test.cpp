#include <catch2/catch_test_macros.hpp>

#include <kernel/virtual_memory.hpp>
#include <undos/syscalls/syscalls.h>

namespace {
[[maybe_unused]] kstd::error_code Unmap(void *, size_t,
                       kernel::vmm::AddressSpace &) noexcept {
  return kernel::KernelError::Success;
}
}
#define KE_VMM_UnmapBorrowed Unmap

#include "object_manager.hpp"
#include "stdkrn.hpp"
#include "ob_test_support.hpp"
#include "sched_test_hooks.hpp"
#include <syscalls/syscalls.hpp>

#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/kobject/KThreadObject.hpp>

namespace {
[[maybe_unused]] kernel::VmmRegionResult Map(void *address, size_t,
                            kernel::vmm::AddressSpace &) noexcept {
  return address;
}

[[maybe_unused]] kstd::error_code CopyFrom(kernel::vmm::AddressSpace &, void *dst,
                          const void *src, size_t size) noexcept {
  __builtin_memcpy(dst, src, size);
  return kernel::KernelError::Success;
}

[[maybe_unused]] kstd::error_code CopyTo(kernel::vmm::AddressSpace &, void *dst,
                        const void *src, size_t size) noexcept {
  __builtin_memcpy(dst, src, size);
  return kernel::KernelError::Success;
}
}

#define KE_VMM_MapBorrowed Map
#define KE_VMM_CopyFromUser CopyFrom
#define KE_VMM_CopyToUser CopyTo
#include "generated_invalidate_rect_dispatch.inc"
#undef KE_VMM_CopyToUser
#undef KE_VMM_CopyFromUser
#undef KE_VMM_MapBorrowed
#undef KE_VMM_UnmapBorrowed

TEST_CASE("Generated InvalidateRect V2 dispatch rejects invalid erase values",
          "[user][invalidate][dispatch]") {
  EnsureObjectManagerInitialized();
  auto createdProcess = kernel::CreateKObject<kernel::KProcessObject>(
      kstd::static_string<64>("invalidate-dispatch-process"));
  REQUIRE(createdProcess);
  auto process = *createdProcess;
  auto createdThread = kernel::CreateKObject<kernel::KThreadObject>(
      kstd::static_string<64>("invalidate-dispatch-thread"));
  REQUIRE(createdThread);
  auto thread = *createdThread;
  thread->owner_process = process.get();
  test_sched::SetCurrentThread(thread);
  struct ResetThread {
    ~ResetThread() { test_sched::SetCurrentThread(nullptr); }
  } reset;

  for (const uint32_t erase : {2u, UINT32_MAX}) {
    __SYS_USER_INVALIDATERECT_V2 wire{};
    wire.erase = erase;
    REQUIRE(Dispatch_InvalidateRect_V2(&wire, sizeof(wire), 0) ==
            kernel::KernelError::InvalidArgument);
  }
}
