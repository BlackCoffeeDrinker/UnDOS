#include <Kernel.hpp>
#include <kernel/hal_interface.hpp>


UNDOS_KERNEL_API [[noreturn]] void kernel_core_main(const kernel::boot_info_t &boot_info) {
  // PHASE 1: Core Hardware Substrate
  // GDT, IDT (masked), PMM initialized, and PIC/APIC remapped. Interrupts are CLEARED (cli).
  HAL_PlatformInit(boot_info);

  // PHASE 2: Early Virtual Memory
  // VMM Stage 1 bootstraps using boot_info.mapped_memory to protect running code.
  // This turns on a primitive heap so kmalloc() works.
  //VMM_EarlyInit(boot_info);

  // PHASE 3: Structural Subsystem
  // Object manager spins up using early kmalloc allocations.
  //ObjectManager::Initialize();

  // VMM Stage 2 links itself as a system object inside the Object Manager.
  //VMM_Finalize();

  // PHASE 4: Storage & Drivers
  // Virtual File System infrastructure layers are prepared.
  //VFS_SetupShell();

  // Initialize Core System Clock (PIT or HPET) so timing facilities exist.
  //HAL_InitializeSystemTimer();

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
    asm volatile("hlt");
  };
}
