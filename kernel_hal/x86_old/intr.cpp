
#include "intr.hpp"

#include <kernel/dispatch.hpp>
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

  // Interrupt masks (IMR). Every hardware line now gets a real IDT stub
  // (hw_irq_handler<IRQ> below) that no-ops when nothing is connected, so
  // there is no longer a need to keep lines masked "just in case" -- a
  // driver unmasks its own line on demand via HAL_INTR_ConnectInterrupt.
  // Unmask only IRQ0 (PIT, always running) and the cascade line (IRQ2,
  // required for any slave-PIC line 8-15 to ever reach the CPU); mask
  // everything else by default.
  //   Master mask 0xFA = 1111 1010b -> IRQ0,2 enabled, IRQ1,3-7 masked.
  //   Slave  mask 0xFF -> IRQ8-15 all masked.
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0xFA)), "Nd"(static_cast<uint16_t>(0x21)));
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

// Vectors 33-47: Generic Hardware IRQ 1-15 Dispatchers.
//
// IRQ0 keeps its bespoke pit_irq_handler above (EOI first, then
// KE_TIME_UpdateSystemTime, preserved verbatim). Every other hardware line
// shares this template-instantiated stub: acknowledge the PIC(s) immediately,
// capture an interrupt record, push it to the kernel dispatch service, and
// return immediately without running driver routines inside the IDT gate.
template<uint8_t IRQ>
[[gnu::interrupt]] void hw_irq_handler(stack_frame *f) {
  kstd::ignore = f;
  pic_send_eoi(IRQ);

  kernel::HalInterruptRecord record{};
  record.vector = 32 + IRQ;
  record.irq = IRQ;
  record.timestamp = 0;
  record.flags = 0;
  KE_DISPATCH_EnqueueInterrupt(record);
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

  // 2. Register Remapped Hardware Vectors (32+). IRQ0 keeps its dedicated
  //    handler; IRQ1-15 each get a template-instantiated hw_irq_handler<N>
  //    stub dispatching through g_hardware_interrupts[N]. remap_pic() above
  //    still masks everything except IRQ0/IRQ1/IRQ2, so most of these lines
  //    are inert until HAL_INTR_ConnectInterrupt unmasks them.
  idt.set(32, KERNEL_GDT_SELECTOR, &pit_irq_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(33, KERNEL_GDT_SELECTOR, &hw_irq_handler<1>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(34, KERNEL_GDT_SELECTOR, &hw_irq_handler<2>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(35, KERNEL_GDT_SELECTOR, &hw_irq_handler<3>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(36, KERNEL_GDT_SELECTOR, &hw_irq_handler<4>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(37, KERNEL_GDT_SELECTOR, &hw_irq_handler<5>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(38, KERNEL_GDT_SELECTOR, &hw_irq_handler<6>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(39, KERNEL_GDT_SELECTOR, &hw_irq_handler<7>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(40, KERNEL_GDT_SELECTOR, &hw_irq_handler<8>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(41, KERNEL_GDT_SELECTOR, &hw_irq_handler<9>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(42, KERNEL_GDT_SELECTOR, &hw_irq_handler<10>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(43, KERNEL_GDT_SELECTOR, &hw_irq_handler<11>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(44, KERNEL_GDT_SELECTOR, &hw_irq_handler<12>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(45, KERNEL_GDT_SELECTOR, &hw_irq_handler<13>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(46, KERNEL_GDT_SELECTOR, &hw_irq_handler<14>, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(47, KERNEL_GDT_SELECTOR, &hw_irq_handler<15>, GateType::INTERRUPT_32, IDTFlags::PRESENT);

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

// HAL_INTR_SetIrqMask: read-modify-write the Interrupt Mask Register of
// whichever PIC owns `irq` (master 0x21 for IRQ 0-7, slave 0xA1 for IRQ
// 8-15). Unmasking a slave line (8-15) also unmasks the master's cascade
// line (IRQ2), since a slave IRQ can never reach the CPU while IRQ2 is
// masked.
//
// Defined at global scope (matching hal_interface.hpp's asm-named
// declarations) rather than inside namespace hal::x86 -- see HAL_IO_In8 and
// friends in ia32.cpp for the same pattern -- so the plain, unmangled
// symbol name is what actually gets exported and cross-stitched by Stage
// 1.5 into kernel_core's undefined references.
UNDOS_HAL_API_DEF void HAL_INTR_SetIrqMask(uint32_t irq, bool masked) noexcept {
  if (irq >= 16) return;

  const uint16_t port = irq < 8 ? 0x21 : 0xA1;
  const auto bit = static_cast<uint8_t>(1u << (irq % 8));

  uint8_t imr = HAL_IO_In8(port);
  if (masked) {
    imr |= bit;
  } else {
    imr &= static_cast<uint8_t>(~bit);
  }
  HAL_IO_Out8(port, imr);

  if (irq >= 8 && !masked) {
    constexpr auto cascade_bit = static_cast<uint8_t>(1u << 2);
    uint8_t master_imr = HAL_IO_In8(0x21);
    master_imr &= static_cast<uint8_t>(~cascade_bit);
    HAL_IO_Out8(0x21, master_imr);
  }
}

// HAL_INTR_ConnectInterrupt: bind an already-constructed
// KInterruptServiceRoutineObject to `irq`. Rejects an already-occupied
// line (returns false) so two drivers can never silently clobber each
// other's ISR; the caller (KE_IO_ConnectInterrupt) is responsible for
// keeping the KObjectPtr alive for as long as the line stays connected --
// only the raw pointer is stored here, matching how g_hardware_interrupts
// already held IRQ1's ISR before this task.
UNDOS_HAL_API_DEF bool HAL_INTR_ConnectInterrupt(
    uint32_t irq,
    const kernel::KObjectPtr<kernel::KInterruptServiceRoutineObject> &isr) noexcept {
  if (irq >= 16 || !isr) return false;
  if (hal::x86::g_hardware_interrupts[irq] != nullptr) return false;

  isr->vector = 32 + irq;
  hal::x86::g_hardware_interrupts[irq] = isr.get();
  HAL_INTR_SetIrqMask(irq, false);
  return true;
}

// HAL_INTR_DisconnectInterrupt: detach whatever is connected to `irq` (if
// anything) and re-mask the line.
UNDOS_HAL_API_DEF void HAL_INTR_DisconnectInterrupt(uint32_t irq) noexcept {
  if (irq >= 16) return;

  hal::x86::g_hardware_interrupts[irq] = nullptr;
  HAL_INTR_SetIrqMask(irq, true);
}
