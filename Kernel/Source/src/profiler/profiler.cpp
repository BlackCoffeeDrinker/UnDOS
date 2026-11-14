// Kernel -finstrument-functions profiler. Compiled and linked only when the
// CMake option UNDOS_PROFILE is ON; built with -fno-instrument-functions.
//
// Nothing reachable from the hooks may be instrumented. The hooks only touch
// this file's statics, inline asm, and the scheduler's per-CPU context
// (CpuContext.cpp / percpu.cpp are excluded from instrumentation, and the
// g_in_hook guard catches any header inline that slipped through).

#include "profiler.hpp"

#include <Kernel.hpp>
#include <kernel/hal_interface.hpp>
#include <kernel/kobject/KDispatcherHeader.hpp>
#include <kernel/scheduler.hpp>
#include <kernel/time.hpp>

#include "scheduler/CpuContext.hpp"

#define PROF_NOINST __attribute__((no_instrument_function))

namespace kernel::prof {
namespace {

struct ProfRecord {
  uint32_t fn;
  uint32_t calls;
  uint64_t incl;
  uint64_t self;
};

constexpr uint32_t TABLE_SIZE = 4096;
constexpr uint32_t TABLE_MASK = TABLE_SIZE - 1;
constexpr uint32_t STACK_DEPTH = 64;
constexpr uint32_t STACK_SLOTS = 64;

constexpr uint32_t PIT_HZ = 1193182;
// Matches hal::x86 set_system_timer_frequency(100) (pit.cpp).
constexpr uint32_t PIT_RELOAD = PIT_HZ / 100;

enum : uint8_t { TIMER_TSC = 0, TIMER_PIT = 1 };

ProfRecord g_table[TABLE_SIZE];

// Shadow stacks live here rather than in KThreadObject: that struct is in the
// shared Kernel/API header also compiled by the HAL and drivers, so a member
// under a kernel-only define would split its layout.
struct StackSlot {
  const KThreadObject *owner;
  ProfShadowStack stack;
};
StackSlot g_stacks[STACK_SLOTS];
ProfShadowStack g_boot_stack;

volatile uint8_t g_enabled = 0;
uint8_t g_in_hook = 0;
uint8_t g_timer_kind = TIMER_PIT;
uint32_t g_timer_hz = PIT_HZ;
uint32_t g_dropped = 0;
uint32_t g_unmatched = 0;
uint64_t g_pit_last = 0;

PROF_NOINST static inline uint32_t save_flags_cli() {
  uint32_t flags;
  __asm__ volatile("pushfl; popl %0; cli" : "=r"(flags)::"memory");
  return flags;
}

PROF_NOINST static inline void restore_flags(uint32_t flags) {
  __asm__ volatile("pushl %0; popfl" ::"r"(flags) : "memory", "cc");
}

PROF_NOINST static inline void outb(uint16_t port, uint8_t v) {
  __asm__ volatile("outb %0, %1" ::"a"(v), "Nd"(port));
}

PROF_NOINST static inline uint8_t inb(uint16_t port) {
  uint8_t v;
  __asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
  return v;
}

PROF_NOINST static inline uint64_t rdtsc() {
  uint32_t lo, hi;
  __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
  return (static_cast<uint64_t>(hi) << 32) | lo;
}

// Monotonic PIT time in 1.193182 MHz units: scheduler tick count times the
// reload plus the position inside the current period. Channel 0 runs in mode
// 3 (square wave, counter decrements by 2 twice per period), so the read-back
// status OUT bit tells which half of the period we are in. Interrupts are off.
PROF_NOINST static inline uint64_t pit_now() {
  outb(0x43, 0xC2);// read-back: latch count + status, channel 0
  const uint8_t status = inb(0x40);
  const uint8_t lo = inb(0x40);
  const uint8_t hi = inb(0x40);
  uint32_t count = static_cast<uint32_t>(lo) | (static_cast<uint32_t>(hi) << 8);
  if (count > PIT_RELOAD) count = PIT_RELOAD;
  uint32_t within = (PIT_RELOAD - count) / 2;
  if (!(status & 0x80)) within += PIT_RELOAD / 2;
  const uint64_t ticks = cpu::current_context().ticks;
  uint64_t v = ticks * PIT_RELOAD + within;
  // A pending IRQ0 can leave `ticks` one period behind the counter; clamp.
  if (v < g_pit_last) v = g_pit_last;
  g_pit_last = v;
  return v;
}

PROF_NOINST static inline uint64_t now() {
  return g_timer_kind == TIMER_TSC ? rdtsc() : pit_now();
}

PROF_NOINST static inline ProfShadowStack *current_stack() {
  const KThreadObject *thread = cpu::current_context().current.get();
  if (!thread) return &g_boot_stack;
  const uint32_t start = (reinterpret_cast<uintptr_t>(thread) >> 4) % STACK_SLOTS;
  for (uint32_t i = 0; i < STACK_SLOTS; ++i) {
    StackSlot &slot = g_stacks[(start + i) % STACK_SLOTS];
    if (slot.owner == thread) return &slot.stack;
    if (!slot.owner) {
      slot.owner = thread;
      return &slot.stack;
    }
  }
  return nullptr;
}

PROF_NOINST static inline ProfRecord *lookup(uint32_t fn) {
  uint32_t h = ((fn >> 2) * 2654435761u) & TABLE_MASK;
  for (uint32_t i = 0; i < TABLE_SIZE; ++i) {
    ProfRecord &r = g_table[h];
    if (r.fn == fn) return &r;
    if (r.fn == 0) {
      r.fn = fn;
      return &r;
    }
    h = (h + 1) & TABLE_MASK;
  }
  return nullptr;
}

PROF_NOINST static inline void retire(ProfShadowStack *s, uint64_t t) {
  const ProfFrame &f = s->f[--s->depth];
  const uint64_t incl = t - f.start;
  const uint64_t self = incl > f.child ? incl - f.child : 0;
  if (ProfRecord *r = lookup(f.fn)) {
    ++r->calls;
    r->incl += incl;
    r->self += self;
  } else {
    ++g_dropped;
  }
  if (s->depth) s->f[s->depth - 1].child += incl;
}

// ---- dump formatting (no KE_Print) ----

PROF_NOINST static inline size_t put_str(char *out, size_t at, const char *s) {
  while (*s) out[at++] = *s++;
  return at;
}

PROF_NOINST static inline size_t put_dec(char *out, size_t at, uint64_t v) {
  char tmp[20];
  size_t n = 0;
  do {
    tmp[n++] = static_cast<char>('0' + v % 10);
    v /= 10;
  } while (v);
  while (n) out[at++] = tmp[--n];
  return at;
}

PROF_NOINST static inline size_t put_hex8(char *out, size_t at, uint32_t v) {
  static constexpr char digits[] = "0123456789abcdef";
  for (int shift = 28; shift >= 0; shift -= 4) out[at++] = digits[(v >> shift) & 0xF];
  return at;
}

PROF_NOINST static inline uint32_t crc32_update(uint32_t crc, const char *p, size_t n) {
  crc = ~crc;
  for (size_t i = 0; i < n; ++i) {
    crc ^= static_cast<uint8_t>(p[i]);
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
  }
  return ~crc;
}

PROF_NOINST void firmware_write(const char *text, size_t size) noexcept {
  HAL_Debug_FirmwarePrint(text, size);
}

// CPUID exists iff EFLAGS.ID (bit 21) can be toggled (not on a 386).
PROF_NOINST static inline bool has_tsc() {
  uint32_t a, b;
  __asm__ volatile(
      "pushfl\n\tpopl %0\n\tmovl %0, %1\n\txorl $0x200000, %0\n\t"
      "pushl %0\n\tpopfl\n\tpushfl\n\tpopl %0\n\tpushl %1\n\tpopfl"
      : "=&r"(a), "=&r"(b)::"cc");
  if (((a ^ b) & 0x200000) == 0) return false;
  uint32_t eax = 0, ebx, ecx, edx;
  __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
  if (eax < 1) return false;
  eax = 1;
  __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
  return (edx & (1u << 4)) != 0;
}

// Measures the TSC over a 10 ms PIT channel 2 one-shot. Returns 0 if channel 2
// never signals terminal count.
PROF_NOINST static inline uint32_t calibrate_tsc() {
  constexpr uint16_t count = PIT_HZ / 100;
  const uint8_t saved = inb(0x61);
  outb(0x61, static_cast<uint8_t>((saved & ~0x03) | 0x00));// gate low, speaker off
  outb(0x43, 0xB0);                                         // ch2, lo/hi, mode 0
  outb(0x42, count & 0xFF);
  outb(0x42, count >> 8);
  outb(0x61, static_cast<uint8_t>((saved & ~0x02) | 0x01));// gate high: start
  const uint64_t t0 = rdtsc();
  uint32_t spins = 0;
  while (!(inb(0x61) & 0x20)) {
    if (++spins > 50000000u) {
      outb(0x61, saved);
      return 0;
    }
  }
  const uint64_t t1 = rdtsc();
  outb(0x61, saved);
  const uint64_t hz = (t1 - t0) * 100;
  return hz > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<uint32_t>(hz);
}

uint32_t g_dump_every_seconds = 0;
uint32_t g_dump_seq = 0;
// Never signalled: waiting on it with a timeout is a timed sleep.
KDispatcherHeader g_dump_sleep;

void DumpThread(void *) noexcept {
  const auto interval = static_cast<uint64_t>(g_dump_every_seconds) * 1000000000ull;
  for (uint64_t deadline = interval;; deadline += interval) {
    for (;;) {
      const auto now_ns = static_cast<uint64_t>(KE_TIME_GetSystemTime().count());
      if (now_ns >= deadline) break;
      const auto left_ms = static_cast<int64_t>((deadline - now_ns + 999999ull) / 1000000ull);
      (void) KE_SCHED_WaitForHeader(g_dump_sleep, kstd::chrono::milliseconds(left_ms));
    }
    prof_dump();
  }
}

}// namespace

void prof_init() noexcept {
  const uint32_t flags = save_flags_cli();
  g_timer_kind = TIMER_PIT;
  g_timer_hz = PIT_HZ;
  if (has_tsc()) {
    if (const uint32_t hz = calibrate_tsc()) {
      g_timer_kind = TIMER_TSC;
      g_timer_hz = hz;
    }
  }
  g_enabled = 1;
  restore_flags(flags);
}

void prof_pause() noexcept {
  g_enabled = 0;
}

void prof_resume() noexcept {
  g_enabled = 1;
}

void prof_dump(WriteFn write) noexcept {
  if (!write) write = &firmware_write;
  const uint32_t flags = save_flags_cli();
  const uint8_t was_enabled = g_enabled;
  g_enabled = 0;

  uint32_t count = 0;
  for (const auto &r: g_table) {
    if (r.calls) ++count;
  }

  char line[128];
  size_t n = 0;
  n = put_str(line, n, "@@UNDOS-PROF BEGIN v1 seq=");
  n = put_dec(line, n, g_dump_seq++);
  n = put_str(line, n, " timer=");
  n = put_str(line, n, g_timer_kind == TIMER_TSC ? "TSC" : "PIT");
  n = put_str(line, n, " hz=");
  n = put_dec(line, n, g_timer_hz);
  n = put_str(line, n, " count=");
  n = put_dec(line, n, count);
  n = put_str(line, n, " dropped=");
  n = put_dec(line, n, g_dropped);
  n = put_str(line, n, " unmatched=");
  n = put_dec(line, n, g_unmatched);
  n = put_str(line, n, " anchor=");
  n = put_hex8(line, n, reinterpret_cast<uintptr_t>(&prof_init));
  n = put_str(line, n, "\r\n");
  write(line, n);

  uint32_t crc = 0;
  // Each block holds one interval: only records that ran, zeroed once sent.
  for (auto &r: g_table) {
    if (!r.calls) continue;
    n = 0;
    n = put_str(line, n, "P ");
    n = put_hex8(line, n, r.fn);
    line[n++] = ' ';
    n = put_dec(line, n, r.calls);
    line[n++] = ' ';
    n = put_dec(line, n, r.incl);
    line[n++] = ' ';
    n = put_dec(line, n, r.self);
    n = put_str(line, n, "\r\n");
    crc = crc32_update(crc, line, n);
    write(line, n);
    r.calls = 0;
    r.incl = 0;
    r.self = 0;
  }
  g_dropped = 0;
  g_unmatched = 0;

  n = 0;
  n = put_str(line, n, "@@UNDOS-PROF END count=");
  n = put_dec(line, n, count);
  n = put_str(line, n, " crc=");
  n = put_hex8(line, n, crc);
  n = put_str(line, n, "\r\n");
  write(line, n);

  g_enabled = was_enabled;
  restore_flags(flags);
}

void prof_schedule_dump(uint32_t seconds) noexcept {
  if (!seconds) return;
  g_dump_every_seconds = seconds;
  if (auto thread = KE_SCHED_CreateThread(cfunc<void(void *)>(&DumpThread), nullptr, ThreadPriority::Idle); !thread) {
    KE_Print("Profiler: failed to start dump thread: {}", thread.error().message());
  }
}

}// namespace kernel::prof

using namespace kernel::prof;

extern "C" PROF_NOINST void __cyg_profile_func_enter(void *fn, void *) {
  const uint32_t flags = save_flags_cli();
  if (!g_enabled || g_in_hook) {
    restore_flags(flags);
    return;
  }
  g_in_hook = 1;
  if (ProfShadowStack *s = current_stack()) {
    if (s->depth >= STACK_DEPTH) {
      ++s->overflow;
    } else {
      ProfFrame &f = s->f[s->depth++];
      f.fn = reinterpret_cast<uintptr_t>(fn);
      f.child = 0;
      f.start = now();
    }
  } else {
    ++g_dropped;
  }
  g_in_hook = 0;
  restore_flags(flags);
}

extern "C" PROF_NOINST void __cyg_profile_func_exit(void *fn, void *) {
  const uint32_t flags = save_flags_cli();
  if (!g_enabled || g_in_hook) {
    restore_flags(flags);
    return;
  }
  g_in_hook = 1;
  const uint64_t t = now();
  if (ProfShadowStack *s = current_stack()) {
    if (s->overflow) {
      --s->overflow;
    } else {
      const uint32_t key = reinterpret_cast<uintptr_t>(fn);
      int match = -1;
      for (int i = s->depth - 1; i >= 0; --i) {
        if (s->f[i].fn == key) {
          match = i;
          break;
        }
      }
      if (match < 0) {
        ++g_unmatched;
      } else {
        // Frames above the match were left without an exit (unwound); retire
        // them at the same timestamp so their parents stay consistent.
        while (s->depth > static_cast<uint16_t>(match)) retire(s, t);
      }
    }
  }
  g_in_hook = 0;
  restore_flags(flags);
}

// Called by HAL_PLATFORM_Panic through a weak reference.
extern "C" UNDOS_KERNEL_API_DEF void KE_PROFILER_OnPanic(void (*write)(const char *, size_t) noexcept) noexcept {
  prof_pause();
  prof_dump(write);
}
