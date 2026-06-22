#include "gdt.hpp"
#include "structs.hpp"

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

  enum {
    GDT_MAX_DESCRIPTORS = N,
  };

  // Note: Ensure PAGING_PAGE_SIZE is defined (e.g., 4096)
  [[gnu::aligned(4096)]] gdt_entry_t gdt[N];
  gdtr_t gdtr;

  constexpr GDT() : gdt{}, gdtr(0, sizeof(gdt_entry_t) * N - 1) {}

  // Basic validity check for 32-bit protected mode
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

    // Load the GDTR into the CPU
    __asm__ volatile("lgdt %0" : : "m"(gdtr));

    // Flush the segment registers and perform a simulated far jump to reload CS
    __asm__ volatile(
        "mov %w0, %%ds\n"
        "mov %w0, %%es\n"
        "mov %w0, %%fs\n"
        "mov %w0, %%gs\n"
        "mov %w0, %%ss\n"
        "pushl %1\n" // Push the target Code Segment selector (0x08)
        "pushl $1f\n"// Push the address of the local forward label '1'
        "lret\n"     // Perform a far return (simulating far jump)
        "1:\n"
        :
        : "r"(OFFSET_KERNEL_DATA), "r"(static_cast<uint32_t>(OFFSET_KERNEL_CODE))
        : "memory");
  }
};

// Instantiate a GDT manager with 5 slots matching your offsets
GDT<5> kernel_gdt;

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

  kernel_gdt.install();
}
}// namespace kernel::gdt
