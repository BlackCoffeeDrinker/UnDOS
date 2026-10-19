#include "stdkrn.hpp"

#include "event_broadcaster.hpp"

#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/mutex.hpp>
#include <kernel/spinlock.hpp>

#include <list.hpp>
#include <mutex.hpp>

namespace {

// A registered listener. Lives in s_subscribers; the node address is never
// handed out, callers only ever see the opaque `id`.
struct EventSubscriber {
  uint64_t id{0};
  kernel::event::EventCallbackFn callback{};
  kernel::event::Source source{};
  kernel::DataBuffer userBuffer{};

  // Set by KE_EVENT_UnregisterInterest: no new dispatch may pick this
  // subscriber up, but the node stays alive until activeCalls drops to zero.
  bool dead{false};
  // Number of callbacks currently executing for this subscriber. Guarded by
  // s_subscribersLock; pinned across the (unlocked) callback invocation so the
  // node - and the DataBuffer it owns - cannot be freed under a running
  // callback.
  uint32_t activeCalls{0};
};

// One queued broadcast. Deliberately holds no reference to any subscriber:
// the fan-out happens on the pump thread, so there is nothing here that can
// dangle when a subscriber unregisters.
struct BroadcastRecord {
  kernel::event::Source source{};
  kernel::event::Code event{};
};

// The queue is reachable from interrupt context (a driver completing an I/O in
// its ISR is the motivating case), so it is a fixed-size ring guarded by an
// IRQ-masking spinlock: no allocation, no yielding, no subscriber-list walk.
constexpr size_t kEventQueueCapacity = 64;
// Bound on how many subscribers a single source may fan out to in one
// dispatch. Keeps the id snapshot on the pump thread's stack.
constexpr size_t kMaxFanOut = 32;

kernel::SpinLock s_queueLock;
BroadcastRecord s_queue[kEventQueueCapacity];
size_t s_queueHead = 0;// next slot to pop
size_t s_queueCount = 0;
uint32_t s_droppedEvents = 0;
bool s_dropWarned = false;

kernel::KDispatcherHeader s_pumpWake{};

// Guards s_subscribers / s_nextTokenId. Only ever taken from thread context
// (the pump thread, and registration/unregistration callers).
kernel::Mutex s_subscribersLock;
kstd::list<EventSubscriber> s_subscribers;
uint64_t s_nextTokenId = 1;
kernel::KObjectPtr<kernel::KThreadObject> s_eventPumpThread;
uint64_t s_dispatchingId = 0;
kernel::KThreadObject *s_dispatchingThread = nullptr;

// Caller must hold s_subscribersLock.
bool CallerIsDispatching(const uint64_t id) noexcept {
  if (s_dispatchingId == 0 || s_dispatchingId != id) return false;
  return KE_SCHED_GetCurrentThread().get() == s_dispatchingThread;
}

// Caller must hold s_subscribersLock.
EventSubscriber *FindSubscriberLocked(const uint64_t id) noexcept {
  for (auto &sub: s_subscribers) {
    if (sub.id == id) return &sub;
  }
  return nullptr;
}

// Caller must hold s_subscribersLock. Drops a dispatch pin and reaps the node
// if an unregister was waiting on it.
void ReleaseSubscriberLocked(const uint64_t id) noexcept {
  for (auto it = s_subscribers.begin(); it != s_subscribers.end(); ++it) {
    if (it->id != id) continue;
    if (it->activeCalls > 0) --it->activeCalls;
    if (it->dead && it->activeCalls == 0) {
      s_subscribers.erase(it);
    }
    return;
  }
}

bool PopRecord(BroadcastRecord &out) noexcept {
  const kernel::IrqSpinLockGuard guard(s_queueLock);
  if (s_queueCount == 0) return false;

  // Move out and clear the slot while we still hold the lock, but note the
  // destructor work (releasing the Source's object reference) happens in the
  // caller's thread context, never with interrupts masked in an ISR.
  out = kstd::move(s_queue[s_queueHead]);
  s_queue[s_queueHead] = BroadcastRecord{};
  s_queueHead = (s_queueHead + 1) % kEventQueueCapacity;
  --s_queueCount;
  return true;
}

// Delivers one broadcast to every subscriber interested in its source.
//
// Subscribers are resolved by id, not by pointer: the list lock is dropped
// across each callback (callbacks are allowed to register, unregister and even
// broadcast), so a snapshot of ids plus a re-lookup is the only safe way to
// walk it. Each subscriber is pinned via activeCalls while its callback runs.
void Dispatch(const BroadcastRecord &record) noexcept {
  uint64_t ids[kMaxFanOut];
  size_t idCount = 0;

  {
    kstd::unique_lock guard(s_subscribersLock);
    for (const auto &sub: s_subscribers) {
      if (sub.dead || !(sub.source == record.source)) continue;
      if (idCount == kMaxFanOut) {
        KE_Print("KE_EVENT: fan-out truncated at {} subscribers", kMaxFanOut);
        break;
      }
      ids[idCount++] = sub.id;
    }
  }

  for (size_t i = 0; i < idCount; ++i) {
    kernel::event::EventCallbackFn callback{};
    const kernel::DataBuffer *userBuffer = nullptr;

    {
      kstd::unique_lock guard(s_subscribersLock);
      EventSubscriber *sub = FindSubscriberLocked(ids[i]);
      // Unregistered (or marked dead) while we were dispatching an earlier
      // subscriber - skip it, that is exactly the guarantee unregister makes.
      if (!sub || sub->dead || !sub->callback) continue;
      ++sub->activeCalls;
      callback = sub->callback;
      userBuffer = &sub->userBuffer;
      s_dispatchingId = ids[i];
      s_dispatchingThread = KE_SCHED_GetCurrentThread().get();
    }

    callback(record.source, record.event, *userBuffer);

    {
      kstd::unique_lock guard(s_subscribersLock);
      s_dispatchingId = 0;
      s_dispatchingThread = nullptr;
      ReleaseSubscriberLocked(ids[i]);
    }
  }
}

void EventThread(void *arg) {
  (void) arg;

  while (true) {
    BroadcastRecord record;
    if (!PopRecord(record)) {
      const auto waitStatus = KE_SCHED_WaitForHeader(s_pumpWake, kernel::WAIT_INFINITE);
      if (waitStatus != kernel::KernelError::Success) {
        KE_Print("event pump wait failed: {}", kernel::make_error_code(waitStatus).message());
        KE_SCHED_Yield();
      }
      continue;
    }

    Dispatch(record);

    // Report an overflow once, from thread context, so broadcast() itself
    // stays printf-free and ISR-safe.
    if (!s_dropWarned) {
      uint32_t dropped = 0;
      {
        const kernel::IrqSpinLockGuard guard(s_queueLock);
        dropped = s_droppedEvents;
      }
      if (dropped != 0) {
        s_dropWarned = true;
        KE_Print("KE_EVENT: broadcast queue overflowed, {} event(s) dropped", dropped);
      }
    }
  }
}

}// namespace

namespace kernel::event {
const Category &system_category() noexcept {
  class SystemCategory final : public Category {
public:
    [[nodiscard]] const char *name() const noexcept override { return "system"; }
    [[nodiscard]] kstd::string_view message(int) const noexcept override { return "unknown"; }
    [[nodiscard]] bool equivalent(const Code &) const noexcept override { return false; }
  };

  static SystemCategory s_systemCategory;
  return s_systemCategory;
}

KernelError init() {
  s_pumpWake.manualReset = false;
  s_pumpWake.signalled = false;
  auto thread = KE_SCHED_CreateThread(&EventThread, nullptr, 0);
  if (!thread) return thread.error();
  s_eventPumpThread = *thread;

  // Anything broadcast before now is already queued; make sure the freshly
  // created pump sees it.
  KE_EVENT_Signal(s_pumpWake, false);
  return KernelError::Success;
}

// Safe to call from interrupt context and before init(): this only touches the
// fixed-size ring under an IRQ-masking spinlock and signals a dispatcher
// header. It never allocates, never yields and never walks the subscriber
// list - the pump thread does the fan-out.
KernelError broadcast(const Source &target, Code event) {
  if ((target.type == Source::Type::KObject && !target.kobject) ||
      (target.type == Source::Type::KDevice && !target.kdevice)) {
    return KernelError::InvalidArgument;
  }
  {
    const IrqSpinLockGuard guard(s_queueLock);
    if (s_queueCount == kEventQueueCapacity) {
      // Queue full: drop the newest record. Dropping (rather than overwriting
      // the oldest) avoids releasing a Source's object reference with
      // interrupts masked.
      ++s_droppedEvents;
      return KernelError::QueueFull;
    }

    const size_t tail = (s_queueHead + s_queueCount) % kEventQueueCapacity;
    s_queue[tail].source = target;
    s_queue[tail].event = event;
    ++s_queueCount;
  }

  KE_EVENT_Signal(s_pumpWake, false);
  return KernelError::Success;
}

namespace detail {
bool pump_once() {
  BroadcastRecord record;
  if (!PopRecord(record)) return false;
  Dispatch(record);
  return true;
}

uint32_t dropped_count() {
  const IrqSpinLockGuard guard(s_queueLock);
  return s_droppedEvents;
}

void reset() {
  {
    const IrqSpinLockGuard guard(s_queueLock);
    for (auto &slot: s_queue) slot = BroadcastRecord{};
    s_queueHead = 0;
    s_queueCount = 0;
    s_droppedEvents = 0;
    s_dropWarned = false;
  }

  kstd::unique_lock guard(s_subscribersLock);
  s_dispatchingId = 0;
  s_dispatchingThread = nullptr;
  s_subscribers.clear();
}
}// namespace detail
}// namespace kernel::event

// Public API Implementations
UNDOS_KERNEL_API_DEF kstd::expected<kernel::event::EventInterestToken, kernel::KernelError> KE_EVENT_RegisterInterest(
    const kernel::event::Source &target,
    kernel::event::RegistrationContext &&context) noexcept {

  if (!context.fn) {
    return kstd::unexpected(kernel::KernelError::InvalidArgument);
  }
  if ((target.type == kernel::event::Source::Type::KObject && !target.kobject) ||
      (target.type == kernel::event::Source::Type::KDevice && !target.kdevice)) {
    return kstd::unexpected(kernel::KernelError::InvalidArgument);
  }

  EventSubscriber sub;
  sub.callback = kstd::move(context.fn);
  sub.source = target;
  sub.userBuffer = kstd::move(context.user_buffer);

  uint64_t id = 0;
  {
    kstd::unique_lock guard(s_subscribersLock);
    if (s_nextTokenId == 0) return kstd::unexpected(kernel::KernelError::ResourceLimitExceeded);
    id = s_nextTokenId++;
    sub.id = id;
    s_subscribers.push_back(kstd::move(sub));
  }

  return kernel::event::EventInterestToken{id};
}

UNDOS_KERNEL_API_DEF kernel::KernelError KE_EVENT_UnregisterInterest(kernel::event::EventInterestToken &token) noexcept {
  if (!token) {
    return kernel::KernelError::InvalidArgument;
  }

  // Zero the token ID immediately to prevent recursive unregistration during ~EventInterestToken()
  const uint64_t idToUnregister = token.id;
  token.id = 0;

  kstd::unique_lock guard(s_subscribersLock);

  EventSubscriber *sub = FindSubscriberLocked(idToUnregister);
  if (!sub) return kernel::KernelError::NotFound;

  // From here on no new dispatch will pick this subscriber up.
  sub->dead = true;

  if (sub->activeCalls > 0) {
    if (CallerIsDispatching(idToUnregister)) {
      // A callback is unregistering itself (directly, or via
      // ~EventInterestToken). Waiting for activeCalls to reach zero would wait
      // on ourselves, so leave the node marked dead and let Dispatch's
      // ReleaseSubscriberLocked reap it when the callback returns.
      return kernel::KernelError::Success;
    }

    // Otherwise block until the in-flight callback finishes, so that once this
    // function returns the caller is guaranteed no callback for this token is
    // running or can start. The subscriber may be reaped by the pump while we
    // are unlocked, hence the re-lookup.
    while (true) {
      guard.unlock();
      KE_SCHED_Yield();
      guard.lock();

      sub = FindSubscriberLocked(idToUnregister);
      if (!sub) return kernel::KernelError::NotFound;
      if (sub->activeCalls == 0) break;
    }
  }

  for (auto it = s_subscribers.begin(); it != s_subscribers.end(); ++it) {
    if (it->id == idToUnregister) {
      s_subscribers.erase(it);
      return kernel::KernelError::Success;
    }
  }
  return kernel::KernelError::NotFound;
}
