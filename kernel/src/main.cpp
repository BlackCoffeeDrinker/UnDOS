#include "kernel/elf.hpp"
#include "object_manager.hpp"
#include "vmm.hpp"


#include <Kernel.hpp>
#include <kernel/hal_interface.hpp>


UNDOS_KERNEL_API [[noreturn]] void kernel_core_main(const kernel::BootInfoT &boot_info) {
  for (const auto &module: boot_info.boot_modules) {
    if (module.base_physical == 0) { continue; }
    early_print_fmt("Found module: name = {}, base = 0x{x}, length = 0x{x}\r\n", module.name.data(), static_cast<uintptr_t>(module.base_physical), module.length);
  }

  for (const auto &[name, address]: boot_info.boot_symbols) {
    if (address != 0) {
      early_print_fmt("Found symbol: name = {}, address = 0x{x}\r\n", name.data(), static_cast<uintptr_t>(address));
    }
  }
  
  // PHASE 1: Core Hardware Substrate
  // GDT, IDT (masked), PMM initialized, and PIC/APIC remapped. Interrupts are CLEARED (cli).
  HAL_Platform_Init(boot_info);

  // PHASE 2: Early Virtual Memory
  // VMM Stage 1 bootstraps using boot_info.mapped_memory to protect running code.
  // This turns on a primitive heap so kmalloc() works.
  HAL_VMM_EarlyInit(boot_info);
  kernel::vmm::init(boot_info);

  // PHASE 3: Structural Subsystem
  // Object manager spins up using early kmalloc allocations.
  kernel::object_manager::init();

  // VMM Stage 2 links itself as a system object inside the Object Manager.
  HAL_VMM_FinalizeInit();

  // PHASE 4: Storage & Drivers
  // Virtual File System infrastructure layers are prepared.
  //VFS_SetupShell();

  // Initialize Core System Clock (PIT or HPET) so timing facilities exist.
  HAL_Platform_InitializeSystemTimer();


  // Start the Scheduler


  // Tell the HAL the Object Manager is ready.
  // HAL can now safely allocate object handles for PCI/PCIe buses and drivers.
  //HAL_InjectDrivers();

  // PHASE 5: System Ignition
  // Safe to reclaim Stage 1.5 bootstrap structures/code now!
  //MemoryReclaim_BootStructures(boot_info);

  // Unmask the CPU interrupts (sti). The clock starts ticking, scheduling can begin.
  //HAL_EnableInterrupts();

  while (1) {
    // Welcome to a fully stable system.
    HAL_CPU_Halt();
  };
}
