
#include "ia32.hpp"

#include "atomics.hpp"
#include "cpuid.hpp"
#include "gdt.hpp"
#include "intr.hpp"
#include "pmm.hpp"
#include "vmm.hpp"

#include <Kernel.hpp>

namespace {
hal::x86::CPUID cpu_id{};
}// namespace

UNDOS_HAL_API_DEF void HAL_PLATFORM_Init(const kernel::BootInfoT &boot_info, kernel::vmm::AddressSpace &kernel_address_space) noexcept {
  // Stage 1.5 currently sets the page size to be 4096 bytes
  // Double check
  if (boot_info.page_size != 4096) {
    HAL_PLATFORM_Panic("Stage 1.5 did not set the page size to 4096 bytes", __FILE__, __LINE__);
  }

  auto atomic_platform = hal::x86::AtomicPlatorm::X86_386;
  cpu_id = hal::x86::get_cpuid();

  // CPUID.1:EDX bit 8 (CX8) indicates native CMPXCHG8B support (Pentium+).
  if (cpu_id.has_CMPXCHG8B) {
    atomic_platform = hal::x86::AtomicPlatorm::X86_CMPXCHG8B;
  } else if (cpu_id.family >= 4) {
    atomic_platform = hal::x86::AtomicPlatorm::X86_XADD;
  }

  KE_Print("CPU Family: {}, Model: {}", cpu_id.family, cpu_id.model);

  hal::x86::init_atomic_ops(atomic_platform);
  hal::x86::init_gdt();
  hal::x86::init_idt();
  hal::x86::init_pmm(boot_info);
  hal::x86::init_vmm();

  // Initialize kernel address space
  kernel_address_space.translation_root = HAL_VMM_GetCurrentTranslationRoot();
  kernel_address_space.asid = 0;

  {
    kernel::VirtualAddress highest_addr = 0;

    // Find the highest virtual address
    for (const auto &region: boot_info.mapped_memory) {
      if (region.type == kernel::MappedMemoryRegionType::None) continue;
      if (kernel::VirtualAddress end = region.virtual_base + region.length;
          end > highest_addr) {
        highest_addr = end;
      }
    }

    // Start heap after the last mapped region + one guard page
    kernel_address_space.current_base = (highest_addr + boot_info.page_size - 1)
                                            .align_down(boot_info.page_size) +
                                        boot_info.page_size;
  }

  kernel_address_space.limit = kernel::VirtualAddress(kernel::VirtualAddress::Invalid);
}


UNDOS_HAL_API_DEF uint32_t HAL_PLATFORM_GetCpuCount() noexcept {
  return 1;
}

UNDOS_HAL_API_DEF void HAL_CPU_Halt() noexcept {
  __asm__ volatile("hlt");
}

UNDOS_HAL_API_DEF void HAL_IO_Out8(uint16_t port, uint8_t val) noexcept {
  asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

UNDOS_HAL_API_DEF uint8_t HAL_IO_In8(uint16_t port) noexcept {
  uint8_t ret;
  asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

UNDOS_HAL_API_DEF void HAL_IO_Out16(uint16_t port, uint16_t val) noexcept {
  asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

UNDOS_HAL_API_DEF uint16_t HAL_IO_In16(uint16_t port) noexcept {
  uint16_t ret;
  asm volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

UNDOS_HAL_API_DEF void HAL_IO_Out32(uint16_t port, uint32_t val) noexcept {
  asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

UNDOS_HAL_API_DEF uint32_t HAL_IO_In32(uint16_t port) noexcept {
  uint32_t ret;
  asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
  return ret;
}

UNDOS_HAL_API_DEF void HAL_IO_Delay() noexcept {
  HAL_IO_Out8(0x80, 0);
}

UNDOS_HAL_API_DEF [[noreturn]] void HAL_PLATFORM_Panic(const kstd::string_view &message, const char *file, int line) noexcept {
  // Once the scheduler is running, interrupts are globally enabled, so a
  // timer/keyboard IRQ could otherwise fire mid-print and preempt into
  // another thread while we're panicking, corrupting the output and never
  // coming back to finish the halt below.
  __asm__ volatile("cli");

  KE_Print("--------");
  KE_Print("PANIC: {} at {}:{}", message, file, line);
  KE_Print("System Halted");

  while (true) {
    __asm__ volatile("cli; hlt");
  }
}

UNDOS_HAL_API_DEF void HAL_PLATFORM_InitializeSystemTimer() noexcept {
}

UNDOS_HAL_API_DEF [[noreturn]] void HAL_PLATFORM_Shutdown() noexcept {
  HAL_IO_Out16(0x604, 0x2000);
  while (true) {
    __asm__ volatile("cli; hlt");
  }
}

namespace {
// Probes for the PCI configuration mechanism #1 via ports 0xCF8/0xCFC. On a
// machine with a PCI host bridge, the enable bit (0x80000000) written to the
// address port reads back unchanged; on a PCI-less machine (e.g. QEMU's
// `isapc`) it does not.
bool HAL_PciPresent() noexcept {
  constexpr uint16_t PCI_CONFIG_ADDRESS = 0xCF8;
  constexpr uint32_t PCI_ENABLE_BIT = 0x80000000u;

  HAL_IO_Out32(PCI_CONFIG_ADDRESS, PCI_ENABLE_BIT);
  const uint32_t readback = HAL_IO_In32(PCI_CONFIG_ADDRESS);
  return readback == PCI_ENABLE_BIT;
}
}// namespace

UNDOS_HAL_API_DEF void HAL_PLATFORM_AfterObjectManager() noexcept {
  if (HAL_PciPresent()) {
    // TODO: A PCI bus is present. A future PCI bus driver would enumerate its
    // configuration space, discover the PCI-to-ISA bridge, and spawn a bridge
    // PDO for the ISA bus driver to attach to. Not implemented yet.
    KE_Print("HAL: PCI bus detected (PCI enumeration not implemented yet)");
  } else {
    // No PCI bus: spawn a legacy ISA root PDO so the ISA bus driver can bind.
    KE_Print("HAL: No PCI bus; reporting legacy ISA root device");
    KE_PNP_ReportRootDevice("bus_isa");
  }
}

namespace std {
[[noreturn]] void terminate() noexcept {
  HAL_PLATFORM_Panic("std::terminate() called", __FILE__, __LINE__);
}
}// namespace std
