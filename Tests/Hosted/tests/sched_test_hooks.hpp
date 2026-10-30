#pragma once

#include <kernel/kobject/KObjectT.hpp>

namespace kernel {
struct KThreadObject;
}

// Test-only control over the host's stand-in for KE_SCHED_GetCurrentThread
// (implemented in kernel_host_stubs.cpp). Null, the default, means no thread.
namespace test_sched {

void SetCurrentThread(kernel::KObjectPtr<kernel::KThreadObject> thread) noexcept;

// One-shot cooperative scheduling at a contended kernel::Mutex.
void SetYieldHook(void (*hook)()) noexcept;

}// namespace test_sched
