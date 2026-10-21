#pragma once

#include <stddef.h>
#include <stdint.h>

#include <kernel/memory/address.hpp>
#include <kernel/kerror.hpp>
#include <expected.hpp>

namespace kernel {
struct KThreadObject;
}

namespace kernel::sched {

// Bring the scheduler up on the bootstrap processor: stamp the per-CPU
// contexts, adopt the currently-running execution stream as CPU 0's first
// thread, and give CPU 0 an idle thread. Must be called with interrupts
// disabled, before any KE_SCHED_CreateThread call.
void init_bsp() noexcept;

// Same, for an application processor that has just been started: adopt the
// stream it arrived on as that CPU's current thread and give it its own idle
// thread. Must be called with interrupts disabled, on the AP itself.
void init_ap(uint32_t cpu_id) noexcept;

void tick() noexcept;

void schedule_next() noexcept;

// Interrupts MUST be disabled. Marks the current thread blocked and switches
// away; returns once another party has made it ready again.
void block_current_locked() noexcept;

// Interrupts MUST be disabled. Moves a blocked thread back to a ready queue,
// picked by pick_target_cpu().
void ready_locked(KThreadObject *thread) noexcept;

// Make `thread` runnable on logical CPU `cpu_id`. Safe to call from any CPU
// and from interrupt context; nudges the target CPU when it is not the
// caller.
void enqueue_on(uint32_t cpu_id, KThreadObject *thread) noexcept;

// The online CPU with the shortest ready queue, or 0 when none is online yet
// (i.e. during early boot, before init_bsp has run).
uint32_t pick_target_cpu() noexcept;

// Allocate a fresh, page-backed kernel stack. Returns the allocation base, or
// a null VirtualAddress on failure. Shared with the AP bring-up path, which
// needs a stack before the AP can run any kernel code.
kstd::expected<VirtualAddress, kstd::error_code> alloc_kernel_stack(size_t size) noexcept;

// Default per-thread kernel stack size, also used for AP bootstrap stacks.
inline constexpr size_t KERNEL_STACK_SIZE = 16 * 1024;
}// namespace kernel::sched
