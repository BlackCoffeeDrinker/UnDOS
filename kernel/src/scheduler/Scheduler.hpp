#pragma once

namespace kernel::sched {

// Bring the scheduler up: create the \System\Threads directory and adopt the
// currently-running execution stream as the first (bootstrap) thread. Must be
// called with interrupts disabled, before any KE_SCHED_CreateThread call.
void init() noexcept;

void tick() noexcept;
}// namespace kernel::sched
