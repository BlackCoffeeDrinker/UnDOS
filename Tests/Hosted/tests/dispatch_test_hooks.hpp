#pragma once

#include <kernel/dispatch.hpp>

namespace test_dispatch {

kernel::InterruptRecordRing &GetRing() noexcept;

kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> GetHandler(uint32_t irq) noexcept;

void Reset() noexcept;

void Pump() noexcept;

} // namespace test_dispatch
