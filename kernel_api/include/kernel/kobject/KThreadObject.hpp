#pragma once

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {
struct KProcessObject;

// Thread Control Block. Platform-agnostic: the only CPU state kept here is the
// saved kernel stack pointer (kernel_stack_top). The HAL pushes/pops the full
// register frame on the thread's own kernel stack and exchanges just the SP,
// so this structure is identical across x86/MIPS/ARM/VAX/PPC.
struct KThreadObject : KObjectT<KThreadObject, 1, TYPE_THREAD> {
  enum class State : uint8_t { Ready, Running, Blocked, Terminated };

  State state{State::Ready};
  uint8_t priority{0};                 // single ready queue for now; room to grow
  uint32_t quantum_ticks{0};           // remaining quantum for this slice

  VirtualAddress kernel_stack_top{0};  // saved kernel SP (mutates every switch)
  VirtualAddress kernel_stack_base{0}; // allocation base, for teardown / free
  VirtualAddress kernel_stack_esp0{0}; // fixed top-of-stack; TSS esp0 on ring3->ring0
  PhysicalAddress translation_root{0}; // 0 => kernel address space

  cfunc<void(void *)> entry;           // thread body
  void *argument{nullptr};             // opaque passthrough to entry

  // Non-owning back-pointer to the owning KProcessObject (nullptr for kernel
  // threads). Kept as a raw KObject* to avoid a KThreadObject <-> KProcessObject
  // header cycle; cast back with KObjectPtr<KProcessObject> where needed.
  KProcessObject *owner_process{nullptr};

  KThreadObject *rq_next{nullptr};     // intrusive ready-queue links
  KThreadObject *rq_prev{nullptr};
};

template<>
struct ObjectTypeOf<TYPE_THREAD> {
  using type = KThreadObject;
};

} // namespace kernel
