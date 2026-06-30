#include "gdt.hpp"
#include "structs.hpp"

namespace hal::x86 {

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
