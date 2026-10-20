#pragma once

#include <kernel/dispatch.hpp>

namespace kernel::dispatch {

kstd::error_code init() noexcept;

InterruptRecordRing &get_ring() noexcept;

kstd::error_code enqueue(const HalInterruptRecord &record) noexcept;

kstd::error_code register_handler(uint32_t irq, const KObjectPtr<KInterruptServiceRoutineObject> &isr) noexcept;

kstd::error_code unregister_handler(uint32_t irq) noexcept;

} // namespace kernel::dispatch
