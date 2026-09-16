#pragma once

#include <kernel/dispatch.hpp>

namespace kernel::dispatch {

void init() noexcept;

InterruptRecordRing &get_ring() noexcept;

bool enqueue(const HalInterruptRecord &record) noexcept;

bool register_handler(uint32_t irq, const KObjectPtr<KInterruptServiceRoutineObject> &isr) noexcept;

void unregister_handler(uint32_t irq) noexcept;

} // namespace kernel::dispatch
