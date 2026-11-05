#pragma once

#include "CpuContext.hpp"

namespace kernel::sched::detail {

inline constexpr uint32_t DefaultQuantum = 5;

template <typename GetTranslationRoot,
          typename SwitchAddressSpace, typename GetKernelStack,
          typename SetKernelStack, typename SwitchContext>
void schedule_next(cpu::CpuContext &context,
                   GetTranslationRoot &&get_translation_root,
                   SwitchAddressSpace &&switch_address_space,
                   GetKernelStack &&get_kernel_stack,
                   SetKernelStack &&set_kernel_stack,
                   SwitchContext &&switch_context) noexcept {
  KThreadObject *previous = context.current.get();

  const uintptr_t state = context.ready_lock.lock_irqsave();
  KThreadObject *next = context.ready.pop_next();
  if (next && previous &&
      previous->state == KThreadObject::State::Running) {
    previous->state = KThreadObject::State::Ready;
    context.ready.enqueue(previous);
  }
  context.ready_lock.unlock_irqrestore(state);

  if (!next) {
    if (previous) previous->quantum_ticks = DefaultQuantum;
    return;
  }

  next->state = KThreadObject::State::Running;
  next->quantum_ticks = DefaultQuantum;
  context.current = KObjectPtr(next);

  if (next->translation_root != PhysicalAddress(0) &&
      get_translation_root() != next->translation_root)
    switch_address_space(next->translation_root);

  if (previous && previous != next)
    previous->kernel_stack_esp0 = get_kernel_stack();
  set_kernel_stack(next->kernel_stack_esp0);

  if (previous != next)
    switch_context(previous->kernel_stack_top, next->kernel_stack_top);
}

}  // namespace kernel::sched::detail
