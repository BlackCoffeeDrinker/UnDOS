
#pragma once

#include <stdint.h>

#include <kernel/hal/x86/bios.hpp>

namespace hal::x86 {

// Combined register/trap context for a #GP raised while EFLAGS.VM==1.
//
// Layout matches, from low address (the value of ESP when v86_gp_dispatch is
// entered) to high address:
//   - a `pushad` block (EDI, ESI, EBP, ESP(dummy), EBX, EDX, ECX, EAX), then
//   - the CPU-pushed v86 exception frame with error code: error_code, EIP,
//     CS, EFLAGS, ESP, SS, ES, DS, FS, GS (in that order; ES/DS/FS/GS are
//     only pushed by the CPU because EFLAGS.VM was set at fault time).
//
// This is *not* used with [[gnu::interrupt]]: gnu::interrupt-generated
// prologues/epilogues only know about the standard (non-v86) exception frame
// shape and would neither read the extra segment registers nor issue the
// right `iret` to restore them, so vector 13 is handled by a hand-written
// assembly stub (see intr.cpp) that fills in this struct directly.
struct [[gnu::packed]] v86_context_t {
  uint32_t edi, esi, ebp, esp_dummy, ebx, edx, ecx, eax;
  uint32_t error_code;
  uint32_t eip;
  uint32_t cs;
  uint32_t eflags;
  uint32_t esp;
  uint32_t ss;
  uint32_t es;
  uint32_t ds;
  uint32_t fs;
  uint32_t gs;
};

static_assert(sizeof(v86_context_t) == 32 + 40);

// Registers the raw-assembly vector-13 (#GP) entry stub; called once from
// init_idt(). Non-v86 (EFLAGS.VM==0) #GP faults keep panicking exactly as
// before.
void init_v86_monitor() noexcept;

}// namespace hal::x86

// C++ side of the vector-13 dispatch. Only called by the raw-assembly stub
// (intr.cpp) when the trapped EFLAGS.VM bit is set. Emulates the fixed
// opcode set (CLI/STI/PUSHF/POPF/IN/OUT/INT n/IRET) and advances v86 IP so
// the stub can resume v86 execution, or, on reaching the stub page's HLT,
// completes the in-flight HAL_x86_ExecBiosInt call and never returns to the
// stub (control is redirected straight back into HAL_x86_ExecBiosInt's
// caller-side stack).
//
// Their definitions (v86.cpp) are marked [[gnu::used]]: their only
// "call"/"use" site is inside a raw __asm__ string, invisible to the
// compiler's (and LTO's) dead-code analysis, so without it they could be
// optimized away even though the linker needs them to resolve the stub.
extern "C" void v86_gp_dispatch(hal::x86::v86_context_t *ctx) noexcept;

// Fallback for a #GP raised outside v86 mode (EFLAGS.VM==0); preserves
// today's panic behavior for regular ring0/ring3 general-protection faults.
// C++ side of the IRQ0-15 stubs when the interrupted EFLAGS.VM==1: reflects
// the IRQ into the BIOS IVT, or defers it while virtual IF is clear.
extern "C" void v86_irq_dispatch(hal::x86::v86_context_t *ctx) noexcept;

extern "C" [[noreturn]] void v86_non_v86_gp_panic(uint32_t error_code, uint32_t eip) noexcept;

// Entry trampoline: pushes the current v86 session's iret frame (filled in
// by HAL_x86_ExecBiosInt via the g_v86_* globals below) onto the CALLER's own
// stack and `iret`s into v86 mode, exactly like HAL_CPU_InitUserThreadContext
// does for a brand-new ring-3 thread -- except here the same kernel thread
// resumes right where it left off (via the completion path in
// v86_gp_dispatch) once the v86 session finishes, so this looks like an
// ordinary blocking function call to HAL_x86_ExecBiosInt.
extern "C" void hal_x86_v86_enter() noexcept;

// Single in-flight v86 session state (synchronous, non-reentrant by design;
// hardware IRQs taken during the call are reflected to the BIOS, never to
// kernel handlers).
// Referenced by name from raw asm (hal_x86_v86_enter/v86_gp_stub), so every
// one of these is marked [[gnu::used]] to survive LTO.
extern "C" {
extern HAL_x86_BiosRegs *g_v86_regs_ptr;
extern uint32_t g_v86_saved_esp0;
extern uint32_t g_v86_resume_esp;
extern uint32_t g_v86_virtual_if;
extern int32_t g_v86_pending_irq;

extern uint32_t g_v86_eax, g_v86_ebx, g_v86_ecx, g_v86_edx, g_v86_esi, g_v86_edi;
extern uint32_t g_v86_cs, g_v86_eip, g_v86_eflags, g_v86_esp, g_v86_ss;
extern uint32_t g_v86_es, g_v86_ds, g_v86_fs, g_v86_gs;
}
