
#include "intr.hpp"

#include <kernel/hal_interface.hpp>
#include <kernel/kobject/KInterruptServiceRoutineObject.hpp>
#include <kernel/syscall.hpp>
#include <kernel/time.hpp>
#include <kernel/virtual_memory.hpp>

#include "__tuple/ignore.hpp"
#include "strfmt.hpp"
#include "structs.hpp"
#include "v86.hpp"

namespace hal::x86 {
using uword_t = unsigned int;

static ISR<256> idt;
static constexpr uint16_t KERNEL_GDT_SELECTOR = 0x8;

// An array of pointers to our KINTERRUPT-style objects for hardware lines (IRQs 0-15)
// Remapped to vectors 32 through 47
static kstd::array<kernel::KInterruptServiceRoutineObject *, 16> g_hardware_interrupts;

// Helper to tell the PIC chips that we finished handling an interrupt
inline void pic_send_eoi(uint8_t irq_line) noexcept {
  if (irq_line >= 8) {
    asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0x20)), "Nd"(static_cast<uint16_t>(0xA0)));// Slave PIC
  }
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0x20)), "Nd"(static_cast<uint16_t>(0x20)));// Master PIC
}

// Low-level hardware configuration to remap the PIC away from CPU Exceptions
void remap_pic() noexcept {
  // ICW1: Start initialization in cascade mode
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0x11)), "Nd"(static_cast<uint16_t>(0x20)));
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0x11)), "Nd"(static_cast<uint16_t>(0xA0)));

  // ICW2: Vector Offsets. Map Master PIC to 0x20 (32) and Slave to 0x28 (40)
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0x20)), "Nd"(static_cast<uint16_t>(0x21)));
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0x28)), "Nd"(static_cast<uint16_t>(0xA1)));

  // ICW3: Tell Master PIC that there is a slave PIC at IRQ2 (0x04)
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0x04)), "Nd"(static_cast<uint16_t>(0x21)));
  // Tell Slave PIC its cascade identity (0x02)
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0x02)), "Nd"(static_cast<uint16_t>(0xA1)));

  // ICW4: Set 8086 mode
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0x01)), "Nd"(static_cast<uint16_t>(0x21)));
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0x01)), "Nd"(static_cast<uint16_t>(0xA1)));

  // Interrupt masks (IMR). Only vectors 32 (IRQ0/PIT) and 33 (IRQ1/keyboard)
  // have dedicated handlers; every other line falls through to the panicking
  // catch-all handler. Unmask just IRQ0, IRQ1 and the cascade line (IRQ2) on
  // the master and keep the whole slave PIC masked, so a stray device IRQ
  // (e.g. the ATA IRQ14 latched during boot IDENTIFY) can't panic the kernel
  // the moment interrupts are enabled for scheduling.
  //   Master mask 0xF8 = 1111 1000b -> IRQ0,1,2 enabled, IRQ3-7 masked.
  //   Slave  mask 0xFF -> IRQ8-15 all masked.
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0xF8)), "Nd"(static_cast<uint16_t>(0x21)));
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0xFF)), "Nd"(static_cast<uint16_t>(0xA1)));
}

// Vector 32: The System PIT Timer (IRQ 0)
[[gnu::interrupt]] void pit_irq_handler(stack_frame *f) {
  kstd::ignore = f;

  // 1. Acknowledge the hardware first. KE_TIME_UpdateSystemTime may perform a
  //    scheduler context switch and not return to this handler until this
  //    thread is rescheduled, so the EOI (and any clock driver work) must have
  //    already happened.
  pic_send_eoi(0);

  // 2. Clear down any registered drivers listening to the clock via kinterrupt_t
  if (g_hardware_interrupts[0]) {
    auto *intro_obj = g_hardware_interrupts[0];
    intro_obj->service_routine(intro_obj, intro_obj->service_context);
  }

  // 3. Call the executive kernel timekeeper (drives preemption). This may switch
  //    away to another thread; control resumes here when we are scheduled again.
  KE_TIME_UpdateSystemTime();
}

// Vector 33: The Keyboard or Generic IRQ 1 Shared Dispatcher
[[gnu::interrupt]] void generic_irq1_handler(stack_frame *f) {
  kstd::ignore = f;
  pic_send_eoi(1);

  if (auto *intro_obj = g_hardware_interrupts[1]) {
    intro_obj->service_routine(intro_obj, intro_obj->service_context);
  }
}

// [Exception and Page Fault Handlers remain exactly as you wrote them]
[[gnu::interrupt]] void interrupt_handler(stack_frame *f) {
  // These handlers are fatal (they always end in HAL_PLATFORM_Panic), and by
  // the time we get here interrupts are globally enabled (the scheduler
  // turns them on once threads start running). Without disabling them first,
  // a timer/keyboard IRQ can fire mid-panic -- e.g. while early_print_fmt is
  // still formatting -- and preempt into the scheduler, corrupting whatever
  // we were in the middle of printing and never coming back.
  __asm__ volatile("cli");
  early_print_fmt("Unhandled Interrupt/Exception at EIP: 0x{:x}\n\r", f->eip);
  HAL_PLATFORM_Panic("Unhandled Interrupt/Exception", __FILE__, __LINE__);
}
[[gnu::interrupt]] void exception_handler(stack_frame *frame, uword_t error_code) {
  kstd::ignore = frame;
  __asm__ volatile("cli");
  early_print_fmt("Exception: 0x{:x}\n\r", error_code);
  HAL_PLATFORM_Panic("Unhandled Exception", __FILE__, __LINE__);
}

[[gnu::interrupt]] void page_fault_handler(stack_frame *frame, uword_t error_code) {
  // Decode CR2 (faulting virtual address) and the error code pushed by the
  // CPU, then immediately hand off to the generic, architecture-agnostic
  // kernel page-fault dispatcher. Bit layout of the i386 page-fault error
  // code:
  //   bit 0 - Present (0 = non-present page, 1 = protection violation)
  //   bit 1 - Write (0 = read, 1 = write)
  //   bit 2 - User (0 = supervisor mode, 1 = user mode)
  //   bit 4 - Instruction fetch (requires NX support)
  uint32_t faulting_address;
  __asm__ volatile("mov %%cr2, %0" : "=r"(faulting_address));

  const kernel::PageFaultInfo info{
      .fault_addr = kernel::VirtualAddress(static_cast<VirtualAddressT>(faulting_address)),
      .present = (error_code & 0x1) != 0,
      .write = (error_code & 0x2) != 0,
      .user = (error_code & 0x4) != 0,
      .exec = (error_code & 0x10) != 0,
      .ip = kernel::VirtualAddress(static_cast<VirtualAddressT>(frame->eip)),
  };

  KE_VMM_HandlePagefault(info);
}

// Vector 13: #GP entry stub.
//
// A #GP raised while EFLAGS.VM==1 (virtual-8086 mode, used by the v86
// monitor -- see v86.cpp/v86.hpp) pushes 4 *extra* segment registers
// (ES, DS, FS, GS) beyond the usual privilege-change exception frame
// (error code, EIP, CS, EFLAGS, ESP, SS). [[gnu::interrupt]]-generated
// prologues/epilogues have no notion of this extended v86 frame shape (they
// would neither read nor restore ES/DS/FS/GS, and the compiler-emitted
// `iret` would consume the wrong number of stack words), so vector 13 is a
// hand-written assembly stub, styled like syscall_handler above.
//
// EFLAGS is always at a fixed offset (12 bytes) above the incoming ESP
// regardless of whether SS/ESP/segment registers were also pushed, so its
// VM bit (0x20000) can be tested immediately to pick a path:
//   - VM==0: today's panic behavior, preserved via v86_non_v86_gp_panic.
//   - VM==1: `pushad` (its layout exactly matches hal::x86::v86_context_t)
//     then dispatch to v86_gp_dispatch, which either emulates the trapped
//     instruction in place (so `popad` + `iret` resumes v86), or -- on the
//     stub page's HLT -- completes the in-flight HAL_x86_ExecBiosInt call
//     and never returns here at all.
extern "C" void v86_gp_stub();
__asm__(
    ".text\n\t"
    ".global v86_gp_stub\n\t"
    ".type v86_gp_stub, @function\n\t"
    "v86_gp_stub:\n\t"
    "    testl $0x20000, 12(%esp)\n\t"
    "    jz    1f\n\t"
    "    pushal\n\t"
    "    pushl %esp\n\t"
    "    call  v86_gp_dispatch\n\t"
    "    addl  $4, %esp\n\t"
    "    popal\n\t"
    "    addl  $4, %esp\n\t"// skip the error code, leaving ESP at EIP
    "    iret\n\t"
    "1:\n\t"
    "    movl  (%esp), %eax\n\t"// error_code
    "    movl  4(%esp), %edx\n\t"// eip
    "    pushl %edx\n\t"
    "    pushl %eax\n\t"
    "    call  v86_non_v86_gp_panic\n\t"
    ".size v86_gp_stub, . - v86_gp_stub\n\t");

// Vector 0x80: int 0x80 syscall gate (DPL3, callable from ring 3).
//
// gnu::interrupt handlers don't expose general-purpose registers, but the
// syscall ABI here is EAX=number, EBX/ECX/EDX=args, so this vector is a raw
// assembly stub: save the caller-visible GPRs, call into the C++ dispatcher
// with cdecl args, stash the i32 result back into EAX, restore GPRs, iret.
extern "C" void syscall_handler();
__asm__(
    ".text\n\t"
    ".global syscall_handler\n\t"
    ".type syscall_handler, @function\n\t"
    "syscall_handler:\n\t"
    "    pushl %ebx\n\t"
    "    pushl %ecx\n\t"
    "    pushl %edx\n\t"
    "    pushl %esi\n\t"
    "    pushl %edi\n\t"
    "    pushl %ebp\n\t"
    "    pushl %edx\n\t"// flags
    "    pushl %ecx\n\t"// pointer to __SYS_CALL_DATA
    "    pushl %ebx\n\t"// size
    "    pushl %eax\n\t"// number
    "    call KE_SYSCALL_Dispatch\n\t"
    "    addl $16, %esp\n\t"
    "    popl %ebp\n\t"
    "    popl %edi\n\t"
    "    popl %esi\n\t"
    "    popl %edx\n\t"
    "    popl %ecx\n\t"
    "    popl %ebx\n\t"
    "    iret\n\t"
    ".size syscall_handler, . - syscall_handler\n\t");

void init_idt() {
  // Remap hardware lines away from the Exception vector zones
  remap_pic();

  // 1. Flood the entire IDT with the baseline handler as a safety net
  for (uint16_t i = 0; i < 256; ++i) {
    idt.set(
        static_cast<uint8_t>(i),
        KERNEL_GDT_SELECTOR,
        &interrupt_handler,
        GateType::INTERRUPT_32,
        IDTFlags::PRESENT);
  }

  // Map the exceptions
  idt.set(8, KERNEL_GDT_SELECTOR, &exception_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(10, KERNEL_GDT_SELECTOR, &exception_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(11, KERNEL_GDT_SELECTOR, &exception_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(12, KERNEL_GDT_SELECTOR, &exception_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(14, KERNEL_GDT_SELECTOR, &page_fault_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(17, KERNEL_GDT_SELECTOR, &exception_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(30, KERNEL_GDT_SELECTOR, &exception_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);

  // 2. Register Remapped Hardware Vectors (32+)
  idt.set(32, KERNEL_GDT_SELECTOR, &pit_irq_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(33, KERNEL_GDT_SELECTOR, &generic_irq1_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);

  // Vector 13: #GP. Raw-assembly stub (see above) so v86-mode faults with
  // their extended (ES/DS/FS/GS) frame can be handled by the v86 monitor;
  // non-v86 #GPs still panic exactly as before.
  idt.idt[13] = idt_entry_t(
      reinterpret_cast<uintptr_t>(&v86_gp_stub),
      KERNEL_GDT_SELECTOR,
      GateType::INTERRUPT_32,
      IDTFlags::PRESENT);

  // Vector 0x80: syscall gate. DPL3 so ring-3 user code may trigger it directly.
  idt.idt[0x80] = idt_entry_t(
      reinterpret_cast<uintptr_t>(&syscall_handler),
      KERNEL_GDT_SELECTOR,
      GateType::INTERRUPT_32,
      IDTFlags::PRESENT | IDTFlags::DPL3);

  idt.set();// Load IDTR



  // Turn interrupts on globally now that the safety net is built!
  //asm volatile("sti");
}
}// namespace hal::x86
