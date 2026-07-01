#include "driver_loader.hpp"
#include "object_manager.hpp"
#include "pnp_manager.hpp"
#include "vmm.hpp"

#include <Kernel.hpp>


uint32_t g_page_size;

UNDOS_KERNEL_API [[noreturn]] void kernel_core_main(const kernel::BootInfoT &boot_info) {
  // PHASE 1: Core Hardware Substrate
  // GDT, IDT (masked), PMM initialized, and PIC/APIC remapped. Interrupts are CLEARED (cli).
  HAL_Platform_Init(boot_info);

  g_page_size = boot_info.page_size;

  // PHASE 2: Early Virtual Memory
  // VMM Stage 1 bootstraps using boot_info.mapped_memory to protect running code.
  // This turns on a primitive heap so kmalloc() works.
  HAL_VMM_EarlyInit(boot_info);
  kernel::vmm::init(boot_info);

  // Initialize driver loader symbol table
  Ke_Driver_Init(boot_info);

  // PHASE 3: Structural Subsystem
  // Object manager spins up using early kmalloc allocations.
  ObInit();
  Ke_PNP_Init();

  // Test: Load the bus_isa driver
  for (const auto &module: boot_info.boot_modules) {
    if (module.base_physical == 0) {
      continue;
    }

    if (module.name == "bus_isa") {
      early_print_fmt("Loading BUS ISA\r\n");
      if (kernel::KObjectPtr driver_obj = kernel::KE_CreateObject<kernel::KDriverObject>()) {
        early_print_fmt("Created Object OKAY\r\n");
        if (auto res = Ke_Drv_LoadDriverModule(module.base_physical.as_ptr<uint8_t>(), module.length, driver_obj);
            res == kernel::ElfResult::Success) {
          early_print("Successfully loaded bus_isa driver!\r\n");
          if (driver_obj->entry_point) {
            driver_obj->entry_point(driver_obj);
          }
        } else {
          early_print_fmt("Failed to load bus_isa driver: {}\n", static_cast<int>(res));
        }
      }
      break;
    }
  }

  early_print("Finalizing VMM...\r\n");
  // VMM Stage 2 links itself as a system object inside the Object Manager.
  kernel::vmm::late_init();
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
