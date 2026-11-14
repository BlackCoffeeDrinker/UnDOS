#pragma once

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {
struct KProcessObject;

// Scheduling level; the ready queue always runs the highest ready level first,
// round-robin within a level. Values are ordered but not contiguous
// (Realtime leaves a gap): a ready queue indexed by priority must map them
// to dense slots rather than use the raw value as an array index.
enum class ThreadPriority : uint8_t {
  Idle = 0,     // only runs when nothing at Normal or above is ready (idle thread, background pumps)
  Normal = 1,   // boot, user and ordinary kernel worker threads
  High = 2,     // latency-sensitive workers (input decoding)
  Realtime = 10,// interrupt dispatch, ahead of everything else
};

// Thread Control Block. Platform-agnostic: the only CPU state kept here is the
// saved kernel stack pointer (kernel_stack_top). The HAL pushes/pops the full
// register frame on the thread's own kernel stack and exchanges just the SP,
// so this structure is identical across x86/MIPS/ARM/VAX/PPC.
struct KThreadObject : KObjectT<KThreadObject, 1, TYPE_THREAD> {
  using KObjectT::KObjectT;
  enum class State : uint8_t { Ready, Running, Blocked, Terminated };

  State state{State::Ready};
  ThreadPriority priority{ThreadPriority::Idle};
  uint32_t quantum_ticks{0};           // remaining quantum for this slice

  VirtualAddress kernel_stack_top{0};  // saved kernel SP (mutates every switch)
  VirtualAddress kernel_stack_base{0}; // allocation base, for teardown / free
  VirtualAddress kernel_stack_esp0{0}; // TSS esp0 restored when scheduled (saved live on switch-out)
  PhysicalAddress translation_root{0}; // 0 => kernel address space

  cfunc<void(void *)> entry;           // thread body
  void *argument{nullptr};             // opaque passthrough to entry

  // Non-owning back-pointer to the owning KProcessObject (nullptr for kernel
  // threads). Kept as a raw KObject* to avoid a KThreadObject <-> KProcessObject
  // header cycle; cast back with KObjectPtr<KProcessObject> where needed.
  KProcessObject *owner_process{nullptr};

  // User threads: signalled (manual-reset) when the thread retires, so a
  // thread handle is waitable; exit_code is valid once it is signalled.
  KDispatcherHeader exitHeader;
  uint32_t exit_code{0};
  uint32_t tid{0};// GetCurrentThreadId; unique among live threads
  // Set while this thread tears itself down; its own waits are never cancelled.
  bool retiring{false};
  // Set around user-facing waits (WaitForObject, message queues). When the
  // owning process exits, such a wait returns early so the thread can retire.
  bool alertable{false};

  KThreadObject *rq_next{nullptr};     // intrusive ready-queue links
  KThreadObject *rq_prev{nullptr};
};

template<>
struct ObjectTypeOf<TYPE_THREAD> {
  using type = KThreadObject;
};

} // namespace kernel
