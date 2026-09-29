#pragma once

namespace kernel {
struct KThreadObject;
}

namespace kernel::sched {

// Bring the scheduler up: create the \System\Threads directory and adopt the
// currently-running execution stream as the first (bootstrap) thread. Must be
// called with interrupts disabled, before any KE_SCHED_CreateThread call.
void init() noexcept;

void tick() noexcept;

void schedule_next() noexcept;

// Interrupts MUST be disabled. Marks the current thread blocked and switches
// away; returns once another party has made it ready again.
void block_current_locked() noexcept;

// Interrupts MUST be disabled. Moves a blocked thread back to the ready queue.
void ready_locked(KThreadObject *thread) noexcept;
}// namespace kernel::sched
