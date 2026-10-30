#include "v86.hpp"

#include <Kernel.hpp>

#include <tuple.hpp>

#include "tss.hpp"
#include "vmm.hpp"

using kernel::PhysicalAddress;
using kernel::VirtualAddress;

namespace hal::x86 {
namespace {

// The v86 stub page: a permanently-resident, identity-mapped 3-byte real-mode
// code snippet `INT n; HLT`, patched per call with the target interrupt
// number. Placed well above the IVT/BDA (0x0-0x4FF) and below any Video BIOS
// ROM shadow area typically starting at 0xC0000, deep inside conventional
// memory so it never collides with real BIOS data structures.
constexpr uint16_t V86_STUB_SEGMENT = 0x2000;// linear 0x00020000
constexpr uint16_t V86_STUB_OFFSET = 0x0000;

// A small, separate area of the same identity-mapped low-memory segment used
// as the v86 code's own real-mode stack (SS:SP) -- large enough for the
// handful of far-call/pushf frames a BIOS routine and our INT/IRET emulation
// push while running.
constexpr uint16_t V86_STACK_SEGMENT = V86_STUB_SEGMENT;
constexpr uint16_t V86_STACK_TOP = 0xF000;

constexpr uint32_t EFLAGS_IF = 1u << 9;
constexpr uint32_t EFLAGS_VM = 1u << 17;
constexpr uint32_t EFLAGS_RESERVED = 1u << 1;

inline uint32_t v86_linear(uint16_t segment, uint16_t offset) noexcept {
  return (static_cast<uint32_t>(segment) << 4) + offset;
}

inline volatile uint8_t *v86_byte_ptr(uint16_t segment, uint16_t offset) noexcept {
  return reinterpret_cast<volatile uint8_t *>(v86_linear(segment, offset));
}

inline volatile uint16_t *v86_word_ptr(uint16_t segment, uint16_t offset) noexcept {
  return reinterpret_cast<volatile uint16_t *>(v86_linear(segment, offset));
}

// Idempotently identity-maps 0x00000000-0x000FFFFF (IVT, BDA, Video BIOS
// ROM) so real BIOS code, the IVT, and our own v86 stub/stack pages can be
// addressed directly through segment:offset -> linear -> identical virtual
// address, reachable from CPL3 v86 code. Uses ensure_page_user_writable()
// rather than the generic HAL_VMM_MapPage() because this range is typically
// *already* mapped supervisor-only by the Stage 1.5 boot loader's own
// transitional identity map; HAL_VMM_MapPage() leaves already-present pages
// untouched, which would silently leave those pages inaccessible at CPL3.
// ensure_page_user_writable() instead upgrades permissions in place, and is
// itself idempotent, so repeated calls across multiple HAL_x86_ExecBiosInt
// invocations are cheap after the first.
void ensure_low_memory_identity_mapped() noexcept {
  constexpr uint32_t low_memory_end = 0x00100000;
  constexpr uint32_t page_size = 0x1000;

  for (uint32_t addr = 0; addr < low_memory_end; addr += page_size) {
    ensure_page_user_writable(VirtualAddress(addr), PhysicalAddress(addr));
  }
}

}// namespace

// See v86.hpp: single in-flight v86 session state.
}// namespace hal::x86

// [[gnu::used]] throughout: these globals are only ever referenced by name
// from the raw __asm__ text in hal_x86_v86_enter/v86_gp_stub, invisible to
// the compiler's (and LTO's) dead-code analysis, so without it they would be
// optimized away even though the linker still needs them to resolve those
// assembly stubs.
extern "C" {
[[gnu::used]] HAL_x86_BiosRegs *g_v86_regs_ptr = nullptr;
uint32_t g_v86_saved_esp0 = 0;
[[gnu::used]] uint32_t g_v86_resume_esp = 0;
uint32_t g_v86_virtual_if = 0;
// IRQ line (0-15) taken while the BIOS had virtual IF clear, or -1.
int32_t g_v86_pending_irq = -1;

[[gnu::used]] uint32_t g_v86_eax = 0, g_v86_ebx = 0, g_v86_ecx = 0, g_v86_edx = 0, g_v86_esi = 0, g_v86_edi = 0;
[[gnu::used]] uint32_t g_v86_cs = 0, g_v86_eip = 0, g_v86_eflags = 0, g_v86_esp = 0, g_v86_ss = 0;
[[gnu::used]] uint32_t g_v86_es = 0, g_v86_ds = 0, g_v86_fs = 0, g_v86_gs = 0;
}

// Pushes the v86 session's iret frame (gs, fs, ds, es, ss, esp, eflags, cs,
// eip -- reading straight from the g_v86_* globals set up by
// HAL_x86_ExecBiosInt) onto the CURRENT (caller's) kernel stack, loads the
// v86 GPRs, and `iret`s. Mirrors HAL_CPU_SwitchContext's callee-saved
// register save so that, on completion, v86_gp_dispatch can restore ESP to
// the value saved in g_v86_resume_esp and simply `ret` back here, exactly as
// if this function had returned normally.
__asm__(
    ".text\n\t"
    ".global hal_x86_v86_enter\n\t"
    ".type hal_x86_v86_enter, @function\n\t"
    "hal_x86_v86_enter:\n\t"
    "    pushl %ebx\n\t"
    "    pushl %esi\n\t"
    "    pushl %edi\n\t"
    "    pushl %ebp\n\t"
    "    movl %esp, g_v86_resume_esp\n\t"
    "    pushl g_v86_gs\n\t"
    "    pushl g_v86_fs\n\t"
    "    pushl g_v86_ds\n\t"
    "    pushl g_v86_es\n\t"
    "    pushl g_v86_ss\n\t"
    "    pushl g_v86_esp\n\t"
    "    pushl g_v86_eflags\n\t"
    "    pushl g_v86_cs\n\t"
    "    pushl g_v86_eip\n\t"
    "    movl g_v86_ebx, %ebx\n\t"
    "    movl g_v86_ecx, %ecx\n\t"
    "    movl g_v86_edx, %edx\n\t"
    "    movl g_v86_esi, %esi\n\t"
    "    movl g_v86_edi, %edi\n\t"
    "    movl g_v86_eax, %eax\n\t"
    "    iret\n\t"
    ".size hal_x86_v86_enter, . - hal_x86_v86_enter\n\t");

extern "C" [[noreturn, gnu::used]] void v86_non_v86_gp_panic(uint32_t error_code, uint32_t eip) noexcept {
  __asm__ volatile("cli");
  KE_Print("#VEC 0d EIP {:08x} Exception: 0x{:x}", eip, error_code);
  HAL_PLATFORM_Panic("Unhandled Exception", __FILE__, __LINE__);
}

namespace hal::x86 {
namespace {

[[noreturn]] void fatal_unknown_opcode(v86_context_t &ctx, uint8_t opcode) noexcept {
  __asm__ volatile("cli");
  KE_Print("v86: unemulated opcode 0x{:x} at {:x}:{:x}",
           opcode, static_cast<uint16_t>(ctx.cs), static_cast<uint16_t>(ctx.eip));
  HAL_PLATFORM_Panic("v86 monitor: unsupported opcode", __FILE__, __LINE__);
}

// Reached when the v86 stub page's HLT is executed (i.e. the BIOS interrupt
// routine returned). Writes the final register state back into the in-flight
// HAL_x86_BiosRegs, restores the caller's esp0, and redirects control
// straight back into hal_x86_v86_enter's caller (HAL_x86_ExecBiosInt) via
// g_v86_resume_esp -- exactly the mirror image of hal_x86_v86_enter's own
// callee-saved-register save. Never returns to the raw #GP stub.
[[noreturn]] void complete_v86_session(v86_context_t &ctx) noexcept {
  if (auto *regs = g_v86_regs_ptr) {
    regs->ax = static_cast<uint16_t>(ctx.eax);
    regs->bx = static_cast<uint16_t>(ctx.ebx);
    regs->cx = static_cast<uint16_t>(ctx.ecx);
    regs->dx = static_cast<uint16_t>(ctx.edx);
    regs->si = static_cast<uint16_t>(ctx.esi);
    regs->di = static_cast<uint16_t>(ctx.edi);
    regs->es = static_cast<uint16_t>(ctx.es);
    regs->ds = static_cast<uint16_t>(ctx.ds);
    regs->flags = static_cast<uint16_t>(ctx.eflags);
  }

  tss_set_esp0(g_v86_saved_esp0);

  const uint32_t resume_esp = g_v86_resume_esp;
  __asm__ volatile(
      "movl %0, %%esp\n\t"
      "popl %%ebp\n\t"
      "popl %%edi\n\t"
      "popl %%esi\n\t"
      "popl %%ebx\n\t"
      "ret\n\t"
      :
      : "r"(resume_esp)
      : "memory");
  __builtin_unreachable();
}

// Delivers hardware IRQ `irq` to the BIOS handler named by the real-mode IVT
// (INT 08h-0Fh for IRQ0-7, INT 70h-77h for IRQ8-15), exactly as a real-mode
// CPU would: push FLAGS/CS/IP on the v86 stack and clear IF. The BIOS handler
// sends its own EOI through the emulated OUT path.
void reflect_irq(v86_context_t &ctx, uint32_t irq) noexcept {
  const uint8_t vector = irq < 8 ? static_cast<uint8_t>(0x08 + irq)
                                 : static_cast<uint8_t>(0x70 + (irq - 8));
  const auto *ivt_entry = reinterpret_cast<const volatile uint16_t *>(static_cast<uint32_t>(vector) * 4u);

  auto flags = static_cast<uint16_t>(ctx.eflags);
  flags = g_v86_virtual_if ? (flags | EFLAGS_IF) : static_cast<uint16_t>(flags & ~EFLAGS_IF);

  auto sp = static_cast<uint16_t>(ctx.esp);
  const auto ss = static_cast<uint16_t>(ctx.ss);
  sp -= 2;
  *v86_word_ptr(ss, sp) = flags;
  sp -= 2;
  *v86_word_ptr(ss, sp) = static_cast<uint16_t>(ctx.cs);
  sp -= 2;
  *v86_word_ptr(ss, sp) = static_cast<uint16_t>(ctx.eip);
  ctx.esp = (ctx.esp & 0xFFFF0000u) | sp;

  ctx.cs = ivt_entry[1];
  ctx.eip = ivt_entry[0];
  g_v86_virtual_if = 0;
  ctx.eflags |= EFLAGS_IF;
}

// Called after the BIOS sets virtual IF again (STI/POPF/IRET).
void deliver_pending_irq(v86_context_t &ctx) noexcept {
  ctx.eflags |= EFLAGS_IF;
  if (g_v86_virtual_if && g_v86_pending_irq >= 0) {
    const auto irq = static_cast<uint32_t>(g_v86_pending_irq);
    g_v86_pending_irq = -1;
    reflect_irq(ctx, irq);
  }
}
}// namespace

// Called once from init_idt() after the vector-13 raw-assembly stub has been
// installed. The stub itself is wired up directly in intr.cpp (alongside
// the other hand-written gates); this hook exists so future v86 monitor
// state (e.g. pre-warming the stub/stack pages) has a single, documented
// place to be initialized without intr.cpp needing to know v86.cpp internals.
void init_v86_monitor() noexcept {
}

}// namespace hal::x86

// Decodes and emulates exactly the fixed opcode set a real-mode BIOS routine
// running in v86 mode issues that trap with #GP: CLI, STI, PUSHF, POPF,
// IN/OUT (8/16/32-bit forms), REP INS/OUTS string I/O, INT n, IRET, and HLT.
// Optional prefixes: 0x66 (operand-size), 0x67 (address-size), 0xF3 (REP).
// SeaBIOS INT 13h uses e.g. `67 F3 6D` (rep insw with 32-bit EDI/ECX).
extern "C" [[gnu::used]] void v86_gp_dispatch(hal::x86::v86_context_t *ctx) noexcept {
  const auto cs = static_cast<uint16_t>(ctx->cs);
  const auto ip = static_cast<uint16_t>(ctx->eip);

  // Consume a run of prefixes (order and count may vary).
  uint16_t prefix_len = 0;
  bool has_operand_size_prefix = false;
  bool has_address_size_prefix = false;
  bool has_rep_prefix = false;
  for (uint16_t i = 0; i < 6; ++i) {
    const uint8_t b = *hal::x86::v86_byte_ptr(cs, static_cast<uint16_t>(ip + prefix_len));
    if (b == 0x66) {
      has_operand_size_prefix = true;
      prefix_len++;
      continue;
    }
    if (b == 0x67) {
      has_address_size_prefix = true;
      prefix_len++;
      continue;
    }
    if (b == 0xF3) {// REP / REPE
      has_rep_prefix = true;
      prefix_len++;
      continue;
    }
    if (b == 0xF2) {// REPNE — treat like REP for string I/O count
      has_rep_prefix = true;
      prefix_len++;
      continue;
    }
    break;
  }

  const auto opcode_off = static_cast<uint16_t>(ip + prefix_len);
  const uint8_t opcode = *hal::x86::v86_byte_ptr(cs, opcode_off);
  const bool has_prefix = has_operand_size_prefix;// kept name for existing IN/OUT cases
  auto set_ip = [&](uint16_t new_ip) { ctx->eip = new_ip; };

  // Direction flag: DF=1 means decrement pointer after string ops.
  const bool decrement = (ctx->eflags & (1u << 10)) != 0;
  const auto es = static_cast<uint16_t>(ctx->es);
  const auto ds = static_cast<uint16_t>(ctx->ds);
  const auto dx_port = static_cast<uint16_t>(ctx->edx);

  auto string_count = [&]() -> uint32_t {
    if (!has_rep_prefix) {
      return 1;
    }
    // In 16-bit v86 default address size, count is CX; 0x67 selects ECX.
    return has_address_size_prefix ? ctx->ecx : (ctx->ecx & 0xFFFFu);
  };

  auto advance_di = [&](uint32_t step) {
    if (has_address_size_prefix) {
      ctx->edi = decrement ? (ctx->edi - step) : (ctx->edi + step);
    } else {
      const auto di = static_cast<uint16_t>(ctx->edi);
      ctx->edi = (ctx->edi & 0xFFFF0000u) |
                 (decrement ? static_cast<uint16_t>(di - static_cast<uint16_t>(step))
                            : static_cast<uint16_t>(di + static_cast<uint16_t>(step)));
    }
  };

  auto advance_si = [&](uint32_t step) {
    if (has_address_size_prefix) {
      ctx->esi = decrement ? (ctx->esi - step) : (ctx->esi + step);
    } else {
      const auto si = static_cast<uint16_t>(ctx->esi);
      ctx->esi = (ctx->esi & 0xFFFF0000u) |
                 (decrement ? static_cast<uint16_t>(si - static_cast<uint16_t>(step))
                            : static_cast<uint16_t>(si + static_cast<uint16_t>(step)));
    }
  };

  auto finish_rep_count = [&](uint32_t done) {
    if (!has_rep_prefix) {
      return;
    }
    if (has_address_size_prefix) {
      ctx->ecx = ctx->ecx - done;
    } else {
      const auto cx = static_cast<uint16_t>(ctx->ecx);
      ctx->ecx = (ctx->ecx & 0xFFFF0000u) | static_cast<uint16_t>(cx - static_cast<uint16_t>(done));
    }
  };

  auto di_offset = [&]() -> uint32_t {
    return has_address_size_prefix ? ctx->edi : (ctx->edi & 0xFFFFu);
  };
  auto si_offset = [&]() -> uint32_t {
    return has_address_size_prefix ? ctx->esi : (ctx->esi & 0xFFFFu);
  };

  switch (opcode) {
    case 0xFA:// CLI
      g_v86_virtual_if = 0;
      set_ip(static_cast<uint16_t>(ip + prefix_len + 1));
      return;

    case 0xFB:// STI
      g_v86_virtual_if = 1;
      set_ip(static_cast<uint16_t>(ip + prefix_len + 1));
      hal::x86::deliver_pending_irq(*ctx);
      return;

    case 0x9C: {// PUSHF
      const auto sp = static_cast<uint16_t>(ctx->esp - 2);
      auto flags = static_cast<uint16_t>(ctx->eflags);
      flags = g_v86_virtual_if ? (flags | hal::x86::EFLAGS_IF) : static_cast<uint16_t>(flags & ~hal::x86::EFLAGS_IF);
      *hal::x86::v86_word_ptr(static_cast<uint16_t>(ctx->ss), sp) = flags;
      ctx->esp = sp;
      set_ip(static_cast<uint16_t>(ip + prefix_len + 1));
      return;
    }

    case 0x9D: {// POPF
      const auto sp = static_cast<uint16_t>(ctx->esp);
      const uint16_t flags = *hal::x86::v86_word_ptr(static_cast<uint16_t>(ctx->ss), sp);
      g_v86_virtual_if = (flags & hal::x86::EFLAGS_IF) ? 1 : 0;
      ctx->eflags = (ctx->eflags & 0xFFFF0000u) | flags | hal::x86::EFLAGS_IF;
      ctx->esp = static_cast<uint16_t>(sp + 2);
      set_ip(static_cast<uint16_t>(ip + prefix_len + 1));
      hal::x86::deliver_pending_irq(*ctx);
      return;
    }

    case 0xF4:// HLT: BIOS routine returned, complete the call.
      if (g_v86_pending_irq >= 0) {
        // The PIC still holds this IRQ in service. Let the BIOS handle it
        // first; its IRET returns to this HLT, which then completes.
        const auto irq = static_cast<uint32_t>(g_v86_pending_irq);
        g_v86_pending_irq = -1;
        hal::x86::reflect_irq(*ctx, irq);
        return;
      }
      hal::x86::complete_v86_session(*ctx);
      __builtin_unreachable();

    case 0xCD: {// INT n
      const uint8_t vector = *hal::x86::v86_byte_ptr(cs, static_cast<uint16_t>(opcode_off + 1));
      const auto *ivt_entry = reinterpret_cast<const volatile uint16_t *>(static_cast<uint32_t>(vector) * 4u);
      const uint16_t target_ip = ivt_entry[0];
      const uint16_t target_cs = ivt_entry[1];

      auto flags = static_cast<uint16_t>(ctx->eflags);
      flags = g_v86_virtual_if ? (flags | hal::x86::EFLAGS_IF) : static_cast<uint16_t>(flags & ~hal::x86::EFLAGS_IF);

      auto sp = static_cast<uint16_t>(ctx->esp);
      const auto ss = static_cast<uint16_t>(ctx->ss);
      sp -= 2;
      *hal::x86::v86_word_ptr(ss, sp) = flags;
      sp -= 2;
      *hal::x86::v86_word_ptr(ss, sp) = cs;
      sp -= 2;
      *hal::x86::v86_word_ptr(ss, sp) = static_cast<uint16_t>(opcode_off + 2);// return address after "INT n"
      ctx->esp = sp;

      ctx->cs = target_cs;
      set_ip(target_ip);
      g_v86_virtual_if = 0;
      return;
    }

    case 0xCF: {// IRET
      auto sp = static_cast<uint16_t>(ctx->esp);
      const auto ss = static_cast<uint16_t>(ctx->ss);
      const uint16_t new_ip = *hal::x86::v86_word_ptr(ss, sp);
      sp = static_cast<uint16_t>(sp + 2);
      const uint16_t new_cs = *hal::x86::v86_word_ptr(ss, sp);
      sp = static_cast<uint16_t>(sp + 2);
      const uint16_t new_flags = *hal::x86::v86_word_ptr(ss, sp);
      sp = static_cast<uint16_t>(sp + 2);

      ctx->esp = sp;
      ctx->cs = new_cs;
      g_v86_virtual_if = (new_flags & hal::x86::EFLAGS_IF) ? 1 : 0;
      ctx->eflags = (ctx->eflags & 0xFFFF0000u) | new_flags | hal::x86::EFLAGS_IF;
      set_ip(new_ip);
      hal::x86::deliver_pending_irq(*ctx);
      return;
    }

    // IN/OUT -- 8/16-bit immediate-port and DX-port forms, plus a 32-bit
    // variant selected by the 0x66 operand-size prefix.
    case 0xE4: {// IN AL, imm8
      const uint8_t port = *hal::x86::v86_byte_ptr(cs, static_cast<uint16_t>(opcode_off + 1));
      ctx->eax = (ctx->eax & 0xFFFFFF00u) | HAL_IO_In8(port);
      set_ip(static_cast<uint16_t>(ip + prefix_len + 2));
      return;
    }
    case 0xE5: {// IN AX/EAX, imm8
      const uint8_t port = *hal::x86::v86_byte_ptr(cs, static_cast<uint16_t>(opcode_off + 1));
      if (has_prefix) {
        ctx->eax = HAL_IO_In32(port);
      } else {
        ctx->eax = (ctx->eax & 0xFFFF0000u) | HAL_IO_In16(port);
      }
      set_ip(static_cast<uint16_t>(ip + prefix_len + 2));
      return;
    }
    case 0xE6: {// OUT imm8, AL
      const uint8_t port = *hal::x86::v86_byte_ptr(cs, static_cast<uint16_t>(opcode_off + 1));
      HAL_IO_Out8(port, static_cast<uint8_t>(ctx->eax));
      set_ip(static_cast<uint16_t>(ip + prefix_len + 2));
      return;
    }
    case 0xE7: {// OUT imm8, AX/EAX
      const uint8_t port = *hal::x86::v86_byte_ptr(cs, static_cast<uint16_t>(opcode_off + 1));
      if (has_prefix) {
        HAL_IO_Out32(port, ctx->eax);
      } else {
        HAL_IO_Out16(port, static_cast<uint16_t>(ctx->eax));
      }
      set_ip(static_cast<uint16_t>(ip + prefix_len + 2));
      return;
    }
    case 0xEC: {// IN AL, DX
      const auto port = static_cast<uint16_t>(ctx->edx);
      ctx->eax = (ctx->eax & 0xFFFFFF00u) | HAL_IO_In8(port);
      set_ip(static_cast<uint16_t>(ip + prefix_len + 1));
      return;
    }
    case 0xED: {// IN AX/EAX, DX
      const auto port = static_cast<uint16_t>(ctx->edx);
      if (has_prefix) {
        ctx->eax = HAL_IO_In32(port);
      } else {
        ctx->eax = (ctx->eax & 0xFFFF0000u) | HAL_IO_In16(port);
      }
      set_ip(static_cast<uint16_t>(ip + prefix_len + 1));
      return;
    }
    case 0xEE: {// OUT DX, AL
      const auto port = static_cast<uint16_t>(ctx->edx);
      HAL_IO_Out8(port, static_cast<uint8_t>(ctx->eax));
      set_ip(static_cast<uint16_t>(ip + prefix_len + 1));
      return;
    }
    case 0xEF: {// OUT DX, AX/EAX
      const auto out_port = static_cast<uint16_t>(ctx->edx);
      if (has_prefix) {
        HAL_IO_Out32(out_port, ctx->eax);
      } else {
        HAL_IO_Out16(out_port, static_cast<uint16_t>(ctx->eax));
      }
      set_ip(static_cast<uint16_t>(ip + prefix_len + 1));
      return;
    }

    // String I/O — SeaBIOS disk transfers use `rep insw` heavily.
    case 0x6C: {// INSB
      const uint32_t n = string_count();
      for (uint32_t i = 0; i < n; ++i) {
        const uint8_t val = HAL_IO_In8(dx_port);
        *hal::x86::v86_byte_ptr(es, static_cast<uint16_t>(di_offset())) = val;
        advance_di(1);
      }
      finish_rep_count(n);
      set_ip(static_cast<uint16_t>(ip + prefix_len + 1));
      return;
    }
    case 0x6D: {// INSW / INSD
      const uint32_t width = has_operand_size_prefix ? 4u : 2u;
      const uint32_t n = string_count();
      for (uint32_t i = 0; i < n; ++i) {
        if (width == 4) {
          const uint32_t val = HAL_IO_In32(dx_port);
          const auto off = static_cast<uint16_t>(di_offset());
          *hal::x86::v86_byte_ptr(es, off) = static_cast<uint8_t>(val);
          *hal::x86::v86_byte_ptr(es, static_cast<uint16_t>(off + 1)) = static_cast<uint8_t>(val >> 8);
          *hal::x86::v86_byte_ptr(es, static_cast<uint16_t>(off + 2)) = static_cast<uint8_t>(val >> 16);
          *hal::x86::v86_byte_ptr(es, static_cast<uint16_t>(off + 3)) = static_cast<uint8_t>(val >> 24);
        } else {
          const uint16_t val = HAL_IO_In16(dx_port);
          *hal::x86::v86_word_ptr(es, static_cast<uint16_t>(di_offset())) = val;
        }
        advance_di(width);
      }
      finish_rep_count(n);
      set_ip(static_cast<uint16_t>(ip + prefix_len + 1));
      return;
    }
    case 0x6E: {// OUTSB
      const uint32_t n = string_count();
      for (uint32_t i = 0; i < n; ++i) {
        const uint8_t val = *hal::x86::v86_byte_ptr(ds, static_cast<uint16_t>(si_offset()));
        HAL_IO_Out8(dx_port, val);
        advance_si(1);
      }
      finish_rep_count(n);
      set_ip(static_cast<uint16_t>(ip + prefix_len + 1));
      return;
    }
    case 0x6F: {// OUTSW / OUTSD
      const uint32_t width = has_operand_size_prefix ? 4u : 2u;
      const uint32_t n = string_count();
      for (uint32_t i = 0; i < n; ++i) {
        if (width == 4) {
          const auto off = static_cast<uint16_t>(si_offset());
          const uint32_t val =
              static_cast<uint32_t>(*hal::x86::v86_byte_ptr(ds, off)) |
              (static_cast<uint32_t>(*hal::x86::v86_byte_ptr(ds, static_cast<uint16_t>(off + 1))) << 8) |
              (static_cast<uint32_t>(*hal::x86::v86_byte_ptr(ds, static_cast<uint16_t>(off + 2))) << 16) |
              (static_cast<uint32_t>(*hal::x86::v86_byte_ptr(ds, static_cast<uint16_t>(off + 3))) << 24);
          HAL_IO_Out32(dx_port, val);
        } else {
          const uint16_t val = *hal::x86::v86_word_ptr(ds, static_cast<uint16_t>(si_offset()));
          HAL_IO_Out16(dx_port, val);
        }
        advance_si(width);
      }
      finish_rep_count(n);
      set_ip(static_cast<uint16_t>(ip + prefix_len + 1));
      return;
    }

    default:
      hal::x86::fatal_unknown_opcode(*ctx, opcode);
  }
}

// Hardware IRQ taken while EFLAGS.VM==1 (see the IRQ stubs in intr.cpp).
// ctx->error_code holds the IRQ line pushed by the stub.
extern "C" [[gnu::used]] void v86_irq_dispatch(hal::x86::v86_context_t *ctx) noexcept {
  const uint32_t irq = ctx->error_code;
  if (g_v86_virtual_if) {
    hal::x86::reflect_irq(*ctx, irq);
    return;
  }
  // BIOS has interrupts virtually disabled: hold the IRQ (still in service
  // at the PIC) and block further ones until STI/POPF/IRET or completion.
  g_v86_pending_irq = static_cast<int32_t>(irq);
  ctx->eflags &= ~hal::x86::EFLAGS_IF;
}

UNDOS_HAL_API_DEF void HAL_x86_ExecBiosInt(uint8_t interrupt_number, HAL_x86_BiosRegs &regs) noexcept {
  hal::x86::ensure_low_memory_identity_mapped();

  // Patch the permanently-resident stub page: INT <interrupt_number>; HLT
  *hal::x86::v86_byte_ptr(hal::x86::V86_STUB_SEGMENT, hal::x86::V86_STUB_OFFSET + 0) = 0xCD;
  *hal::x86::v86_byte_ptr(hal::x86::V86_STUB_SEGMENT, hal::x86::V86_STUB_OFFSET + 1) = interrupt_number;
  *hal::x86::v86_byte_ptr(hal::x86::V86_STUB_SEGMENT, hal::x86::V86_STUB_OFFSET + 2) = 0xF4;

  g_v86_regs_ptr = &regs;
  g_v86_virtual_if = (regs.flags & (1u << 9)) ? 1u : 0u;
  g_v86_pending_irq = -1;

  g_v86_eax = regs.ax;
  g_v86_ebx = regs.bx;
  g_v86_ecx = regs.cx;
  g_v86_edx = regs.dx;
  g_v86_esi = regs.si;
  g_v86_edi = regs.di;

  g_v86_cs = hal::x86::V86_STUB_SEGMENT;
  g_v86_eip = hal::x86::V86_STUB_OFFSET;
  g_v86_ss = hal::x86::V86_STACK_SEGMENT;
  g_v86_esp = hal::x86::V86_STACK_TOP;
  g_v86_es = regs.es ? regs.es : hal::x86::V86_STUB_SEGMENT;
  g_v86_ds = regs.ds ? regs.ds : hal::x86::V86_STUB_SEGMENT;
  g_v86_fs = hal::x86::V86_STUB_SEGMENT;
  g_v86_gs = hal::x86::V86_STUB_SEGMENT;
  // Real IF is set so hardware IRQs reach the BIOS (e.g. Award INT 13h waits
  // for IRQ14). The IRQ stubs reflect them into the real-mode IVT; the BIOS
  // sees only the virtual IF tracked in g_v86_virtual_if.
  g_v86_eflags = hal::x86::EFLAGS_VM | hal::x86::EFLAGS_IF | hal::x86::EFLAGS_RESERVED |
                 (regs.flags & 0x0CD5u);

  // Point esp0 at a dedicated trap stack, carved out of this call's own
  // stack frame, for the duration of this nested call so a #GP raised by the
  // v86 session doesn't clobber this thread's live kernel stack; the normal
  // esp0 (maintained by the scheduler via HAL_CPU_SetKernelStack) is restored
  // once the call completes. Allocated on demand here rather than as a
  // permanently-resident global, since it is only ever needed for the
  // duration of this function.
  alignas(16) uint8_t trap_stack[4096];
  g_v86_saved_esp0 = hal::x86::tss_get_esp0();
  hal::x86::tss_set_esp0(reinterpret_cast<uintptr_t>(trap_stack + sizeof(trap_stack)));

  HAL_CPU_DisableInterrupts();
  // BIOS disk services need the IDE lines (IRQ14/15, via cascade IRQ2).
  const uint8_t saved_master_mask = HAL_IO_In8(0x21);
  const uint8_t saved_slave_mask = HAL_IO_In8(0xA1);
  HAL_IO_Out8(0x21, static_cast<uint8_t>(saved_master_mask & ~0x04u));
  HAL_IO_Out8(0xA1, static_cast<uint8_t>(saved_slave_mask & ~0xC0u));
  hal_x86_v86_enter();// returns only once the v86 session has completed
  HAL_IO_Out8(0xA1, saved_slave_mask);
  HAL_IO_Out8(0x21, saved_master_mask);
  HAL_CPU_EnableInterrupts();

  g_v86_regs_ptr = nullptr;
}
