#pragma once

// x86-only public HAL contract. Deliberately kept OUTSIDE of
// kernel/hal_interface.hpp: no other HAL port (ARM/MIPS/PPC, and any future
// non-x86 port) is required to implement this. Any caller that includes this
// header is implicitly opting into x86-only support (e.g. a future VBE/INT10h
// graphics driver).

#include <kernel/__core.hpp>

// In/out register block for a single real-mode BIOS interrupt call executed
// via HAL_x86_ExecBiosInt. Callers fill in the fields they need on entry;
// on return the struct reflects the final v86 register state so BIOS return
// values (e.g. VBE mode-info results, or CF for BIOS error signaling via
// `flags`) can be read back.
struct HAL_x86_BiosRegs {
  uint16_t ax = 0, bx = 0, cx = 0, dx = 0;
  uint16_t si = 0, di = 0;
  uint16_t es = 0, ds = 0;// segment regs BIOS calls commonly read/return (e.g. VBE buffer seg)
  uint16_t flags = 0;     // real-mode FLAGS in/out (callers can check CF for BIOS error signaling)
};

/**
 * @ingroup CPU
 * @brief Method HAL_x86_ExecBiosInt
 *
 * Executes a real BIOS interrupt (e.g. 0x10, 0x13) synchronously in hardware
 * virtual-8086 mode and writes the resulting register state back into
 * `regs`. x86 HAL only -- never declared in the generic
 * kernel_api/hal_interface.hpp. Not reentrant/interrupt-safe: interrupts are
 * disabled for the duration of the call.
 */
UNDOS_HAL_PUBLIC_V1API(void, HAL_x86_ExecBiosInt, uint8_t interrupt_number, HAL_x86_BiosRegs &regs);
