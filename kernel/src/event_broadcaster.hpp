#pragma once

#include "stdkrn.hpp"

namespace kernel::event {
/**
 * @brief Starts the event pump thread.
 *
 * broadcast() is usable before this is called - records simply queue up and are
 * delivered once the pump exists.
 */
void init();

/**
 * @brief Queues @p eventId for every subscriber interested in @p target.
 *
 * Threading contract:
 * - **Safe from interrupt context.** Only touches a fixed-size ring under an
 *   IRQ-masking spinlock and signals a dispatcher header. Never allocates,
 *   never yields, never walks the subscriber list.
 * - Delivery is **asynchronous**: callbacks run later on the event pump thread,
 *   so a subscriber registered between this call and the actual dispatch will
 *   still receive the event.
 * - The queue is bounded. If it is full the broadcast is **dropped** and a
 *   warning is printed once; there is deliberately no way to block the caller,
 *   since the caller may be an ISR.
 * - @p target is captured by value, so the source object is kept alive until
 *   the event has been delivered.
 */
void broadcast(const Source &target, Code event);

template<typename T>
  requires kstd::is_base_of_v<KObject, T>
void broadcast(const KObjectPtr<T> &target, Code event) { return broadcast(Source(target), event); }

template<typename T>
  requires kstd::is_base_of_v<KDevice, T>
void broadcast(const KDevicePtr<T> &target, Code event) { return broadcast(Source(target), event); }

namespace detail {
/**
 * @brief Pops and dispatches a single queued broadcast on the calling thread.
 *
 * Exposed so hosted tests can drive delivery synchronously instead of standing
 * up a real pump thread. Returns false when the queue is empty.
 */
bool pump_once();

/// Number of broadcasts discarded because the queue was full.
uint32_t dropped_count();

/// Drops every subscriber and queued record. Test-support only.
void reset();
}// namespace detail
}// namespace kernel::event
