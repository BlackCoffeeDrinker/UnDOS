#include "driver_loader.hpp"
#include "object_manager.hpp"
#include "pnp_manager.hpp"
#include "vmm.hpp"

#include <Kernel.hpp>


uint32_t g_page_size;

namespace {
const char *GetObjectTypeName(kernel::ObjectType type) {
  if (type == kernel::TYPE_DIRECTORY) return "Directory";
  if (type == kernel::TYPE_DEVICE) return "Device";
  if (type == kernel::TYPE_DRIVER) return "Driver";
  if (type == kernel::TYPE_BUS) return "Bus";
  if (type == kernel::TYPE_VMM) return "VMM";
  if (type == kernel::TYPE_MODULE) return "Module";
  return "Unknown";
}

void DumpObjectTree(const kernel::KObjectPtr<kernel::KObject>& obj, int depth = 0) {
  if (!obj) return;

  for (int i = 0; i < depth; ++i) {
    early_print("  ");
  }

  kstd::string_view name = obj->name;
  if (name.empty() && depth == 0) {
    name = "/";
  }

  early_print_fmt("|-- {} ({})\n\r", name, GetObjectTypeName(obj->type));

  if (const auto directory = obj.As<kernel::KDirectoryObject>()) {
    directory->children.each([&](auto entry) {
      DumpObjectTree(entry, depth + 1);
    });
  }
}

void InitBootModulesInOb(const kernel::BootInfoT &boot_info) {
  const auto initialRoot = KE_OB_LookupObjectOfType<kernel::KDirectoryObject>(R"(\System\Initial\BootModules)");
  if (!initialRoot) {
    early_print_fmt("Failed to find initial root directory object\n\r");
    return;
  }

  // Test: Load the bus_isa driver
  for (const auto &module: boot_info.boot_modules) {
    if (module.base_physical == 0) {
      continue;
    }

    if (const auto kmodule = KE_CreateObject<kernel::KModuleObject>()) {
      kmodule->base_physical = module.base_physical;
      kmodule->length = module.length;
      kmodule->name = module.name;

      KE_OB_InsertObject(initialRoot, kmodule);
    }
  }
}
}// namespace

UNDOS_KERNEL_API [[noreturn]] void kernel_core_main(const kernel::BootInfoT &boot_info) {
  // PHASE 1: Core Hardware Substrate
  // GDT, IDT (masked), PMM initialized, and PIC/APIC remapped. Interrupts are CLEARED (cli).
  HAL_PLATFORM_Init(boot_info);

  g_page_size = boot_info.page_size;

  // PHASE 2: Early Virtual Memory
  // VMM Stage 1 bootstraps using boot_info.mapped_memory to protect running code.
  // This turns on a primitive heap so kmalloc() works.
  HAL_VMM_EarlyInit(boot_info);
  kernel::vmm::init(boot_info);

  // Initialize driver loader symbol table
  KE_DRIVER_Init(boot_info);

  // PHASE 3: Structural Subsystem
  // Object manager spins up using early kmalloc allocations.
  ObInit();
  KE_PNP_Init();

  InitBootModulesInOb(boot_info);

  // VMM Stage 2 links itself as a system object inside the Object Manager.
  kernel::vmm::late_init();
  HAL_VMM_FinalizeInit();

  // Initialize Core System Clock (PIT or HPET) so timing facilities exist.
  HAL_PLATFORM_InitializeSystemTimer();

  // PHASE 4: Storage & Drivers
  // Virtual File System infrastructure layers are prepared.
  //VFS_SetupShell();


  // Tell the HAL the Object Manager is ready.
  // HAL can now safely allocate object handles for PCI/PCIe buses and drivers.
  HAL_PLATFORM_AfterObjectManager();

  // PHASE 5: System Ignition
  // Safe to reclaim Stage 1.5 bootstrap structures/code now!
  //MemoryReclaim_BootStructures(boot_info);

  // Unmask the CPU interrupts (sti). The clock starts ticking, scheduling can begin.
  //HAL_EnableInterrupts();

  DumpObjectTree(KE_OB_GetRootDirectory());
  early_print("Done");
  while (1) {
    // Welcome to a fully stable system.
    HAL_PLATFORM_Shutdown();
  };
}
