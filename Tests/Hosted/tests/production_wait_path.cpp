// Keep existing no-thread GUI fixtures usable while linking the real blocking
// wait implementation for schedulable arena contention tests.
#include "stdkrn.hpp"
#include <kernel/scheduler.hpp>
UNDOS_KERNEL_PUBLIC_V1API(kstd::error_code, HostedProductionWaitForHeader,
    kernel::KDispatcherHeader &, kstd::chrono::milliseconds);
#define KE_SCHED_WaitForHeader HostedProductionWaitForHeader
#include "../../../Kernel/Source/src/wait.cpp"
