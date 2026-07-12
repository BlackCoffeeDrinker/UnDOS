#include "gdt.hpp"
#include "structs.hpp"
#include "tss.hpp"

namespace hal::x86 {

template<size_t N>
struct GDT {
  static constexpr Descriptor BASIC_DESCRIPTOR =
      Descriptor::PRESENT |
      Descriptor::READWRITE |
      Descriptor::CODE_DATA |
      Descriptor::ACCESS;

  static constexpr uint16_t OFFSET_KERNEL_CODE = (0x01u * 0x08u);// 0x08
  static constexpr uint16_t OFFSET_KERNEL_DATA = (0x02u * 0x08u);// 0x10
  static constexpr uint16_t OFFSET_USER_DATA = (0x03u * 0x08u);  // 0x18
  static constexpr uint16_t OFFSET_USER_CODE = (0x04u * 0x08u);  // 0x20
  static constexpr uint16_t OFFSET_TSS = (0x05u * 0x08u);        // 0x28

  enum {
    GDT_MAX_DESCRIPTORS = N,
  };

  [[gnu::aligned(4096)]] gdt_entry_t gdt[N];
  gdtr_t gdtr;

  constexpr GDT() : gdt{}, gdtr(0, sizeof(gdt_entry_t) * N - 1) {}

  [[nodiscard]] static constexpr bool is_valid_descriptor(Descriptor d) noexcept {
    return d == static_cast<Descriptor>(0) || has_flag(d, Descriptor::PRESENT);
  }

  constexpr void set_entry(size_t idx,
                           uint32_t base,
                           uint32_t limit,
                           Descriptor access,
                           Granularity gran) noexcept {
    if (idx >= N) return;
    if (!is_valid_descriptor(access)) return;

    gdt[idx] = gdt_entry_t(base, limit, access, gran);
  }

  [[nodiscard]] constexpr const gdt_entry_t *data() const noexcept { return gdt; }
  [[nodiscard]] constexpr size_t size() const noexcept { return N; }

  void install() noexcept {
    gdtr.base = reinterpret_cast<uint32_t>(&gdt[0]);

    __asm__ volatile("lgdt %0" : : "m"(gdtr));

    __asm__ volatile(
        "mov %w0, %%ds\n"
        "mov %w0, %%es\n"
        "mov %w0, %%fs\n"
        "mov %w0, %%gs\n"
        "mov %w0, %%ss\n"
        "pushl %1\n"
        "pushl $1f\n"
        "lret\n"
        "1:\n"
        :
        : "r"(OFFSET_KERNEL_DATA), "r"(static_cast<uint32_t>(OFFSET_KERNEL_CODE))
        : "memory");
  }
};

GDT<6> kernel_gdt;
tss_entry_t kernel_tss;

void init_gdt() {
  // Slot 0: Null Descriptor
  kernel_gdt.set_entry(0, 0, 0, static_cast<Descriptor>(0), static_cast<Granularity>(0));

  // Flat memory model rules for 386/486:
  // Base = 0, Limit = 0xFFFFF. Combined with 4K Granularity, this spans the full 4GB space.
  constexpr uint32_t base = 0x00000000;
  constexpr uint32_t limit = 0xFFFFF;
  constexpr Granularity flags = Granularity::BIG_PAGES_4K | Granularity::X32;

  // Slot 1: Kernel Code
  kernel_gdt.set_entry(1, base, limit,
                       Descriptor::PRESENT | Descriptor::CODE_DATA | Descriptor::EXECUTABLE | Descriptor::READWRITE,
                       flags);

  // Slot 2: Kernel Data
  kernel_gdt.set_entry(2, base, limit,
                       Descriptor::PRESENT | Descriptor::CODE_DATA | Descriptor::READWRITE,
                       flags);

  // Slot 3: User Data (DPL bits are set via Descriptor::DPL)
  kernel_gdt.set_entry(3, base, limit,
                       Descriptor::PRESENT | Descriptor::CODE_DATA | Descriptor::READWRITE | Descriptor::DPL3,
                       flags);

  // Slot 4: User Code
  kernel_gdt.set_entry(4, base, limit,
                       Descriptor::PRESENT | Descriptor::CODE_DATA | Descriptor::EXECUTABLE | Descriptor::READWRITE | Descriptor::DPL3,
                       flags);

  // Slot 5: Task State Segment (32-bit TSS, byte granularity)
  for (auto &b: reinterpret_cast<uint8_t (&)[sizeof(tss_entry_t)]>(kernel_tss)) b = 0;
  kernel_tss.ss0 = GDT<6>::OFFSET_KERNEL_DATA;
  kernel_tss.iomap_base = sizeof(tss_entry_t);

  kernel_gdt.set_entry(5,
                       reinterpret_cast<uint32_t>(&kernel_tss), sizeof(tss_entry_t) - 1,
                       Descriptor::PRESENT | Descriptor::ACCESS | Descriptor::EXECUTABLE,
                       static_cast<Granularity>(0));

  kernel_gdt.install();

  __asm__ volatile("ltr %w0" ::"r"(static_cast<uint16_t>(GDT<6>::OFFSET_TSS)));
}

void tss_set_esp0(uint32_t esp0) noexcept {
  kernel_tss.esp0 = esp0;
}

void reload_gdt() {
  kernel_gdt.install();
}
}// namespace hal::x86

#include <kernel/hal_interface.hpp>

UNDOS_HAL_API_DEF void HAL_CPU_SetKernelStack(kernel::VirtualAddress kernel_stack_top) noexcept {
  hal::x86::tss_set_esp0(static_cast<uint32_t>(kernel_stack_top.value));
}
