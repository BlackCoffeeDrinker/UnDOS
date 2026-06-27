
#include "intr.hpp"

#include <kernel/hal_interface.hpp>
#include <kernel/interrupt.hpp>
#include <kernel/time.hpp>

#include "strfmt.hpp"
#include "structs.hpp"

namespace hal::x86 {
using uword_t = unsigned int;

static ISR<256> idt;
static constexpr uint16_t KERNEL_GDT_SELECTOR = 0x8;

// An array of pointers to our KINTERRUPT-style objects for hardware lines (IRQs 0-15)
// Remapped to vectors 32 through 47
static kernel::InterruptServiceRoutine *g_hardware_interrupts[16] = {nullptr};

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

  // Unmask all hardware interrupts for now (Null masks)
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0x00)), "Nd"(static_cast<uint16_t>(0x21)));
  asm volatile("outb %0, %1" ::"a"(static_cast<uint8_t>(0x00)), "Nd"(static_cast<uint16_t>(0xA1)));
}

// Vector 32: The System PIT Timer (IRQ 0)
[[gnu::interrupt]] void pit_irq_handler(stack_frame *f) {
  // 1. Call the executive kernel timekeeper immediately
  KE_Time_UpdateSystemTime();

  // 2. Clear down any registered drivers listening to the clock via kinterrupt_t
  if (g_hardware_interrupts[0]) {
    auto *intro_obj = g_hardware_interrupts[0];
    intro_obj->service_routine(intro_obj, intro_obj->service_context);
  }

  // 3. Acknowledge the hardware
  pic_send_eoi(0);
}

// Vector 33: The Keyboard or Generic IRQ 1 Shared Dispatcher
[[gnu::interrupt]] void generic_irq1_handler(stack_frame *f) {
  if (g_hardware_interrupts[1]) {
    auto *intro_obj = g_hardware_interrupts[1];
    intro_obj->service_routine(intro_obj, intro_obj->service_context);
  }
  pic_send_eoi(1);
}

// [Exception and Page Fault Handlers remain exactly as you wrote them]
[[gnu::interrupt]] void interrupt_handler(stack_frame *f) {

  early_print_fmt("Unhandled Interrupt/Exception at EIP: 0x{x}\n\r", f->eip);
  HAL_Platform_Panic("Unhandled Interrupt/Exception", __FILE__, __LINE__);
}
[[gnu::interrupt]] void exception_handler(stack_frame *frame, uword_t error_code) {
  early_print_fmt("Exception: 0x{x}\n\r", error_code);
  HAL_Platform_Panic("Unhandled Exception", __FILE__, __LINE__);
}

[[gnu::interrupt]] void page_fault_handler(stack_frame *frame, uword_t error_code) {
  uint32_t faulting_address;
  __asm__ volatile("mov %%cr2, %0" : "=r"(faulting_address));

  early_print_fmt("Faulting address: 0x{x}\n\r", faulting_address);

  HAL_Platform_Panic("Unhandled Page Fault", __FILE__, __LINE__);
}

void init_idt() {
  // Remap hardware lines away from the Exception vector zones
  remap_pic();

  // 1. Flood the entire IDT with the baseline handler as a safety net
  for (size_t i = 0; i < 256; ++i) {
    idt.set(i, KERNEL_GDT_SELECTOR, &interrupt_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  }

  // Map the exceptions
  idt.set(8, KERNEL_GDT_SELECTOR, &exception_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(10, KERNEL_GDT_SELECTOR, &exception_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(11, KERNEL_GDT_SELECTOR, &exception_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(12, KERNEL_GDT_SELECTOR, &exception_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(13, KERNEL_GDT_SELECTOR, &exception_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(14, KERNEL_GDT_SELECTOR, &page_fault_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(17, KERNEL_GDT_SELECTOR, &exception_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(30, KERNEL_GDT_SELECTOR, &exception_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);

  // 2. Register Remapped Hardware Vectors (32+)
  idt.set(32, KERNEL_GDT_SELECTOR, &pit_irq_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);
  idt.set(33, KERNEL_GDT_SELECTOR, &generic_irq1_handler, GateType::INTERRUPT_32, IDTFlags::PRESENT);

  idt.set();// Load IDTR

  // Turn interrupts on globally now that the safety net is built!
  //asm volatile("sti");
}
}// namespace hal::x86
