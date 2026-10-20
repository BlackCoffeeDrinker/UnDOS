#pragma once

#include <catch2/catch_test_macros.hpp>

// Shared test helper: object_manager::init() populates process-wide singleton
// directories and is not safe to call more than once per test binary run (it
// would recreate the singletons and orphan the previous ones). Since this is
// an `inline` function, its local static is a single shared instance across
// every translation unit that includes this header, so all test files can
// safely call it independently regardless of test execution order.
//
// Every KObject (including object_manager::init()'s own well-known
// directories) is heap-allocated via KE_Malloc, which is now the real
// kernel/src/vmm.cpp SLAB allocator (backed by the fake RAM set up in
// kernel_host_vmm_stubs.cpp) rather than a plain malloc() stub. It returns
// nullptr for every request until kernel::vmm::init() has run, so that must
// happen first, regardless of which test file/test case runs first.

#include "object_manager.hpp"
#include "vmm.hpp"

#include <kernel/virtual_memory.hpp>

// Provided by kernel_host_vmm_stubs.cpp: the bounds of the fake RAM buffer
// backing the host test's kernel address space. On real hardware these
// bounds are computed by HAL_PLATFORM_Init() (ia32.cpp) from boot_info's
// mapped memory regions before the object manager/VMM are used.
kernel::VirtualAddress HAL_TEST_GetFakeRamBase() noexcept;
size_t HAL_TEST_GetFakeRamSize() noexcept;

inline void EnsureObjectManagerInitialized() {
  static bool initialized = false;
  if (!initialized) {
    kernel::vmm::init();

    auto &kernel_as = KE_VMM_GetKernelAddressSpace();
    kernel_as.current_base = HAL_TEST_GetFakeRamBase();
    kernel_as.limit = kernel_as.current_base + HAL_TEST_GetFakeRamSize();

    kernel::objectmanager::init();
    kernel::vmm::late_init();
    initialized = true;
  }
}
