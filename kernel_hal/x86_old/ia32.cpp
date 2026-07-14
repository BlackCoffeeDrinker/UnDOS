
#include "ia32.hpp"
#include "gdt.hpp"
#include "intr.hpp"
#include "pmm.hpp"
#include "vmm.hpp"

#include <Kernel.hpp>

namespace {
kernel::VirtualAddress highest_addr{};
}

hal::x86::regs cpu_cpuid(int code) {
  hal::x86::regs r{};
  asm volatile("cpuid" : "=a"(r.eax), "=b"(r.ebx),
                         "=c"(r.ecx), "=d"(r.edx) : "0"(code));
  return r;
}

UNDOS_HAL_API_DEF void HAL_PLATFORM_Init(const kernel::BootInfoT &boot_info) noexcept {
  // Stage 1.5 currently sets the page size to be 4096 bytes
  // Double check
  if (boot_info.page_size != 4096) {
    early_print("Stage 1.5 did not set the page size to 4096 bytes");
    HAL_PLATFORM_Panic("Stage 1.5 did not set the page size to 4096 bytes", __FILE__, __LINE__);
  }

  hal::x86::init_gdt();
  hal::x86::init_idt();
  hal::x86::init_pmm(boot_info);
  hal::x86::init_vmm();

  // Find the highest virtual address
  for (const auto &region: boot_info.mapped_memory) {
    if (region.type == kernel::MappedMemoryRegionType::None) continue;
    if (kernel::VirtualAddress end = region.virtual_base + region.length;
        end > highest_addr) {
      highest_addr = end;
    }
  }
}

UNDOS_HAL_API_DEF kernel::VirtualAddress HAL_VMM_GetHighestVirtualAddress() noexcept {
  return highest_addr;
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

UNDOS_HAL_API_DEF [[noreturn]] void HAL_PLATFORM_Panic(const char *message, const char *file, int line) noexcept {
  // Once the scheduler is running, interrupts are globally enabled, so a
  // timer/keyboard IRQ could otherwise fire mid-print and preempt into
  // another thread while we're panicking, corrupting the output and never
  // coming back to finish the halt below.
  __asm__ volatile("cli");

  early_print_fmt("\r\n--------\r\nPANIC: {} at {}:{}\r\nSystem Halted\r\n", message, file, line);

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
    early_print("HAL: PCI bus detected (PCI enumeration not implemented yet)\r\n");
  } else {
    // No PCI bus: spawn a legacy ISA root PDO so the ISA bus driver can bind.
    early_print("HAL: No PCI bus; reporting legacy ISA root device\r\n");
    KE_PNP_ReportRootDevice("bus_isa");
  }
}

namespace std {
[[noreturn]] void terminate() noexcept {
  HAL_PLATFORM_Panic("std::terminate() called", __FILE__, __LINE__);
}
}// namespace std
