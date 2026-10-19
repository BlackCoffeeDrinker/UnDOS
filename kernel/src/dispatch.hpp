#pragma once

#include <kernel/dispatch.hpp>

namespace kernel::dispatch {

KernelError init() noexcept;

InterruptRecordRing &get_ring() noexcept;

KernelError enqueue(const HalInterruptRecord &record) noexcept;

KernelError register_handler(uint32_t irq, const KObjectPtr<KInterruptServiceRoutineObject> &isr) noexcept;

KernelError unregister_handler(uint32_t irq) noexcept;

} // namespace kernel::dispatch
