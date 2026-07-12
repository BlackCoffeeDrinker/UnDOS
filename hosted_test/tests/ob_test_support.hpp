#pragma once

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

inline void EnsureObjectManagerInitialized() {
  static bool initialized = false;
  if (!initialized) {
    kernel::vmm::init();
    kernel::objectmanager::init();
    kernel::vmm::late_init();
    initialized = true;
  }
}
