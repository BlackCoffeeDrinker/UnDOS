#pragma once

#include <stdint.h>

// Executive (object manager, scheduler, processes, memory) bring-up calls
// used by the kernel core. Everything else stays private to the component.
namespace kernel::objectmanager {
void init();
}

namespace kernel::sched {
// Interrupts disabled, before any KE_SCHED_CreateThread call.
void init_bsp() noexcept;
}

namespace kernel::dispatch {
void init() noexcept;
}

namespace kernel::smp {
void start_secondary_cpus() noexcept;
}

namespace kernel::cpu {
/// Number of CPUs whose scheduler context is online.
uint32_t online_count() noexcept;
}
