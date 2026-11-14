#include <catch2/catch_test_macros.hpp>

#include "object_manager.hpp"
#include "percpu.hpp"
#include "sched_test_hooks.hpp"
#include "scheduler/CpuContext.hpp"
#include "scheduler/Scheduler.hpp"
#include "stdkrn.hpp"
#include "ob_test_support.hpp"
#include "task_manager_scheduler_hal_hooks.hpp"
#include "smp_test_hooks.hpp"

#include <kernel/hal_interface.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/kobject/KThreadObject.hpp>

#include "../../../Kernel/HAL/x86_old/v86_irq_tracking.hpp"

#include <array>

namespace {

struct Fixture {
  kernel::cpu::CpuContext &context = kernel::cpu::contexts()[0];
  kernel::KThreadObject *threads[3]{};
  size_t threadCount{0};

  Fixture() {
    EnsureObjectManagerInitialized();
    test_smp::Reset();
    context.online = true;
    context.id = 0;
    context.current = nullptr;
    context.idle = nullptr;
    context.accounting = {};
  }

  void track(kernel::KThreadObject *thread) { threads[threadCount++] = thread; }

  ~Fixture() {
    const uintptr_t state = context.ready_lock.lock_irqsave();
    for (size_t i = 0; i < threadCount; ++i) context.ready.remove(threads[i]);
    context.ready_lock.unlock_irqrestore(state);
    context.current = nullptr;
    context.idle = nullptr;
    context.accounting = {};
    test_sched::SetCurrentThread(nullptr);
    test_sched_hal::Reset();
    test_smp::Reset();
    context.online = false;
  }
};

}  // namespace

TEST_CASE("Production IRQ0 charging precedes switches and preserves interrupted CS",
          "[task-manager][scheduler][production]") {
  Fixture fixture;
  auto processA = kernel::CreateKObject<kernel::KProcessObject>(
      kstd::static_string<64>("sched-a"));
  auto processB = kernel::CreateKObject<kernel::KProcessObject>(
      kstd::static_string<64>("sched-b"));
  auto processC = kernel::CreateKObject<kernel::KProcessObject>(
      kstd::static_string<64>("sched-c"));
  auto threadA = kernel::CreateKObject<kernel::KThreadObject>(
      kstd::static_string<64>("sched-thread-a"));
  auto threadB = kernel::CreateKObject<kernel::KThreadObject>(
      kstd::static_string<64>("sched-thread-b"));
  auto threadC = kernel::CreateKObject<kernel::KThreadObject>(
      kstd::static_string<64>("sched-thread-c"));
  REQUIRE(processA);
  REQUIRE(processB);
  REQUIRE(processC);
  REQUIRE(threadA);
  REQUIRE(threadB);
  REQUIRE(threadC);

  (*threadA)->owner_process = processA->get();
  (*threadB)->owner_process = processB->get();
  (*threadC)->owner_process = processC->get();
  (*threadA)->state = kernel::KThreadObject::State::Running;
  (*threadA)->priority = kernel::ThreadPriority::Normal;
  (*threadB)->priority = kernel::ThreadPriority::High;
  (*threadC)->priority = kernel::ThreadPriority::High;
  (*threadA)->translation_root = kernel::PhysicalAddress(0x1000);
  (*threadB)->translation_root = kernel::PhysicalAddress(0x2000);
  (*threadC)->translation_root = kernel::PhysicalAddress(0x3000);
  (*threadA)->kernel_stack_top = kernel::VirtualAddress(0x1100);
  (*threadB)->kernel_stack_top = kernel::VirtualAddress(0x2200);
  (*threadC)->kernel_stack_top = kernel::VirtualAddress(0x3300);
  (*threadA)->kernel_stack_esp0 = kernel::VirtualAddress(0x1110);
  (*threadB)->kernel_stack_esp0 = kernel::VirtualAddress(0x2210);
  (*threadC)->kernel_stack_esp0 = kernel::VirtualAddress(0x3310);
  fixture.context.current = *threadA;
  fixture.track(threadA->get());
  fixture.track(threadB->get());
  fixture.track(threadC->get());
  fixture.context.accounting.clock_available = true;
  test_sched::SetCurrentThread(*threadA);

  test_sched_hal::Reset();
  test_sched_hal::SetRoot(kernel::PhysicalAddress(0x1000));
  test_sched_hal::SetStack(kernel::VirtualAddress(0xA000));
  test_sched_hal::SetSavedStack(kernel::VirtualAddress(0xA100));

  fixture.context.ready.enqueue(threadB->get());
  kernel::sched::charge_timer_tick(kernel::InterruptedMode::User);
  kernel::sched::schedule_next();
  REQUIRE(fixture.context.current.get() == threadB->get());
  REQUIRE((*threadA)->state == kernel::KThreadObject::State::Ready);
  REQUIRE(HAL_VMM_GetCurrentTranslationRoot() == kernel::PhysicalAddress(0x2000));
  REQUIRE(HAL_CPU_GetKernelStack() == kernel::VirtualAddress(0x2210));
  REQUIRE((*threadA)->kernel_stack_esp0 == kernel::VirtualAddress(0xA000));
  REQUIRE((*threadA)->kernel_stack_top == kernel::VirtualAddress(0xA100));
  REQUIRE(processA->get()->cpuTicks == 1);
  REQUIRE(processA->get()->userTicks == 1);
  REQUIRE(processB->get()->cpuTicks == 0);
  REQUIRE(fixture.context.accounting.total_ticks == 1);

  fixture.context.ready.enqueue(threadC->get());
  test_smp::SetInterruptsEnabled(false);
  kernel::sched::charge_timer_tick(kernel::InterruptedMode::Kernel);
  REQUIRE_FALSE(test_smp::InterruptsEnabled());
  test_smp::SetInterruptsEnabled(true);
  kernel::sched::schedule_next();
  REQUIRE(fixture.context.current.get() == threadC->get());
  REQUIRE(processB->get()->cpuTicks == 1);
  REQUIRE(processB->get()->kernelTicks == 1);
  REQUIRE(test_sched_hal::SwitchFrom(1) == kernel::VirtualAddress(0x2200));
  REQUIRE(test_sched_hal::SwitchTo(1) == kernel::VirtualAddress(0x3300));

  test_sched_hal::SetHardwareInterruptDepth(1);
  REQUIRE(HAL_CPU_InHardwareInterrupt());
  // Saved CS, not this continuation's IRQ depth, determines process attribution.
  kernel::sched::charge_timer_tick(kernel::InterruptedMode::User);
  kernel::sched::schedule_next();
  REQUIRE(fixture.context.current.get() == threadB->get());
  REQUIRE_FALSE(HAL_CPU_InHardwareInterrupt());
  REQUIRE(processC->get()->cpuTicks == 1);
  REQUIRE(processC->get()->userTicks == 1);
  REQUIRE(fixture.context.accounting.system_ticks == 0);

  kernel::sched::schedule_next();
  REQUIRE(fixture.context.current.get() == threadC->get());
  REQUIRE(HAL_CPU_InHardwareInterrupt());
  REQUIRE(processB->get()->cpuTicks == 1);
  test_sched_hal::SetHardwareInterruptDepth(0);
  REQUIRE_FALSE(HAL_CPU_InHardwareInterrupt());
  REQUIRE(fixture.context.accounting.total_ticks == 3);

  kernel::sched::schedule_next();
  REQUIRE(fixture.context.current.get() == threadB->get());
  REQUIRE(fixture.context.accounting.counter_epoch == 1);
  REQUIRE(processC->get()->cpuTicks == 1);

  kernel::sched::schedule_next();
  REQUIRE(fixture.context.current.get() == threadC->get());
  REQUIRE(processB->get()->cpuTicks == 1);
  REQUIRE(fixture.context.accounting.total_ticks == 3);
  REQUIRE(test_sched_hal::SwitchCount() == 6);
}

TEST_CASE("HAL IRQ depth follows new kernel and user contexts independently",
          "[irq-context][task-manager][scheduler][production]") {
  Fixture fixture;
  auto original = kernel::CreateKObject<kernel::KThreadObject>(
      kstd::static_string<64>("irq-original"));
  auto kernelThread = kernel::CreateKObject<kernel::KThreadObject>(
      kstd::static_string<64>("irq-kernel"));
  auto userThread = kernel::CreateKObject<kernel::KThreadObject>(
      kstd::static_string<64>("irq-user"));
  REQUIRE(original);
  REQUIRE(kernelThread);
  REQUIRE(userThread);

  (*original)->state = kernel::KThreadObject::State::Running;
  (*original)->kernel_stack_top = kernel::VirtualAddress(0x4100);
  test_sched_hal::Reset();
  (*kernelThread)->kernel_stack_top = HAL_CPU_InitThreadContext(
      kernel::VirtualAddress(0x9000), kernel::cfunc<void()>{});
  (*userThread)->kernel_stack_top = HAL_CPU_InitUserThreadContext(
      kernel::VirtualAddress(0xA000), kernel::VirtualAddress(0xB000),
      kernel::VirtualAddress(0xC000));
  (*userThread)->translation_root = kernel::PhysicalAddress(0x4400);
  fixture.context.current = *original;
  fixture.track(original->get());
  fixture.track(kernelThread->get());
  fixture.track(userThread->get());
  fixture.context.ready.enqueue(kernelThread->get());
  fixture.context.ready.enqueue(userThread->get());

  test_sched_hal::SetHardwareInterruptDepth(1);
  REQUIRE(HAL_CPU_InHardwareInterrupt());

  kernel::sched::schedule_next();
  REQUIRE(fixture.context.current.get() == kernelThread->get());
  REQUIRE_FALSE(HAL_CPU_InHardwareInterrupt());

  kernel::sched::schedule_next();
  REQUIRE(fixture.context.current.get() == userThread->get());
  REQUIRE_FALSE(HAL_CPU_InHardwareInterrupt());

  kernel::sched::schedule_next();
  REQUIRE(fixture.context.current.get() == original->get());
  REQUIRE(HAL_CPU_InHardwareInterrupt());
  test_sched_hal::SetHardwareInterruptDepth(0);
  REQUIRE_FALSE(HAL_CPU_InHardwareInterrupt());
}

TEST_CASE("V86 IRQ tracking covers dispatch, pending delivery, IRET, cleanup, and software INT",
          "[irq-context][v86]") {
  test_sched_hal::Reset();
  hal::x86::V86IrqTracking tracking;
  uint32_t pending_irq = UINT32_MAX;

  REQUIRE_FALSE(HAL_CPU_InHardwareInterrupt());
  REQUIRE_FALSE(tracking.begin_hardware_dispatch(false, 5));
  REQUIRE(HAL_CPU_InHardwareInterrupt());
  tracking.end_hardware_dispatch();
  REQUIRE_FALSE(HAL_CPU_InHardwareInterrupt());
  REQUIRE(tracking.take_pending(pending_irq));
  REQUIRE(pending_irq == 5);
  REQUIRE_FALSE(tracking.take_pending(pending_irq));

  const hal::x86::ReflectedIrqFrame pending_frame{0x100, 0x200, 0x300};
  REQUIRE(tracking.begin_reflected(pending_frame));
  REQUIRE(HAL_CPU_InHardwareInterrupt());
  REQUIRE_FALSE(tracking.complete_iret({0x100, 0x200, 0x301}));
  REQUIRE(HAL_CPU_InHardwareInterrupt());
  REQUIRE(tracking.complete_iret(pending_frame));
  REQUIRE_FALSE(HAL_CPU_InHardwareInterrupt());

  const hal::x86::ReflectedIrqFrame direct_frame{0x400, 0x500, 0x600};
  REQUIRE(tracking.begin_hardware_dispatch(true, 1));
  REQUIRE(HAL_CPU_InHardwareInterrupt());
  REQUIRE(tracking.begin_reflected(direct_frame));
  tracking.end_hardware_dispatch();
  REQUIRE(HAL_CPU_InHardwareInterrupt());
  REQUIRE(tracking.reflected_depth() == 1);
  tracking.set_pending(7);
  tracking.complete_session();
  REQUIRE_FALSE(HAL_CPU_InHardwareInterrupt());
  REQUIRE(tracking.reflected_depth() == 0);
  REQUIRE_FALSE(tracking.take_pending(pending_irq));

  tracking.begin_dispatch(
      hal::x86::V86IrqTracking::DispatchKind::SoftwareInt);
  REQUIRE_FALSE(HAL_CPU_InHardwareInterrupt());
  tracking.end_dispatch(
      hal::x86::V86IrqTracking::DispatchKind::SoftwareInt);
  REQUIRE_FALSE(HAL_CPU_InHardwareInterrupt());
}

TEST_CASE("Production tick path charges exact idle and detached owner without allocation",
          "[task-manager][scheduler][production]") {
  Fixture fixture;
  auto process = kernel::CreateKObject<kernel::KProcessObject>("tick-owner");
  auto thread = kernel::CreateKObject<kernel::KThreadObject>("tick-idle");
  REQUIRE(process);
  REQUIRE(thread);
  fixture.context.current = *thread;
  fixture.context.idle = *thread;
  (*thread)->owner_process = process->get();
  kernel::sched::charge_timer_tick(kernel::InterruptedMode::Kernel);
  REQUIRE(fixture.context.accounting.idle_ticks == 0); // accounting not initialized
  fixture.context.accounting.clock_available = true;
  kernel::sched::charge_timer_tick(kernel::InterruptedMode::Kernel);
  REQUIRE(fixture.context.accounting.idle_ticks == 1);
  REQUIRE((*process)->cpuTicks == 0);
  fixture.context.idle = nullptr;
  kernel::sched::charge_timer_tick(kernel::InterruptedMode::Kernel);
  REQUIRE((*process)->kernelTicks == 1);
  (*thread)->owner_process = nullptr;
  kernel::sched::charge_timer_tick(kernel::InterruptedMode::Kernel);
  REQUIRE(fixture.context.accounting.system_ticks == 1);
  fixture.context.current = nullptr;
  kernel::sched::charge_timer_tick(kernel::InterruptedMode::Kernel);
  REQUIRE(fixture.context.accounting.system_ticks == 2);
  REQUIRE(fixture.context.accounting.total_ticks == 4);
  REQUIRE(test_smp::InterruptsEnabled());
}

TEST_CASE("Production scheduler resets a running thread quantum when no peer is ready",
          "[task-manager][scheduler][production]") {
  Fixture fixture;
  auto thread = kernel::CreateKObject<kernel::KThreadObject>(
      kstd::static_string<64>("scheduler-only"));
  REQUIRE(thread);
  (*thread)->state = kernel::KThreadObject::State::Running;
  (*thread)->quantum_ticks = 1;
  fixture.context.current = *thread;
  fixture.track(thread->get());

  test_sched_hal::Reset();
  kernel::sched::schedule_next();
  REQUIRE((*thread)->quantum_ticks == 5);
  REQUIRE(test_sched_hal::SwitchCount() == 0);
}


TEST_CASE("Scheduler hands off from a terminated thread to idle accounting", "[task-manager][scheduler][production]") {
  Fixture fixture;
  auto bootstrap = kernel::CreateKObject<kernel::KThreadObject>("cpu0boot-test");
  auto idle = kernel::CreateKObject<kernel::KThreadObject>("idle-test");
  REQUIRE(bootstrap);
  REQUIRE(idle);

  (*bootstrap)->state = kernel::KThreadObject::State::Running;
  (*bootstrap)->priority = kernel::ThreadPriority::Normal;
  (*bootstrap)->translation_root = kernel::PhysicalAddress(0);
  (*bootstrap)->kernel_stack_top = kernel::VirtualAddress(0x5100);
  (*idle)->state = kernel::KThreadObject::State::Ready;
  (*idle)->priority = kernel::ThreadPriority::Idle;
  (*idle)->translation_root = kernel::PhysicalAddress(0);
  (*idle)->kernel_stack_top = kernel::VirtualAddress(0x6100);

  fixture.context.current = *bootstrap;
  fixture.context.idle = *idle;
  fixture.context.accounting.clock_available = true;
  fixture.track(bootstrap->get());
  fixture.track(idle->get());
  fixture.context.ready.enqueue(idle->get());

  test_sched_hal::Reset();
  test_sched_hal::SetStack(kernel::VirtualAddress(0xA000));
  (*bootstrap)->state = kernel::KThreadObject::State::Terminated;
  kernel::sched::schedule_next();
  REQUIRE(fixture.context.current.get() == idle->get());
  REQUIRE((*bootstrap)->state == kernel::KThreadObject::State::Terminated);
  REQUIRE(fixture.context.ready.empty());

  kernel::sched::charge_timer_tick(kernel::InterruptedMode::Kernel);
  kernel::sched::charge_timer_tick(kernel::InterruptedMode::Kernel);
  kernel::sched::charge_timer_tick(kernel::InterruptedMode::Kernel);
  REQUIRE(fixture.context.accounting.idle_ticks == 3);
  REQUIRE(fixture.context.accounting.system_ticks == 0);
  REQUIRE(fixture.context.accounting.total_ticks == 3);
}
