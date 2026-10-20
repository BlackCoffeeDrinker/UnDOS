
#include "driver_loader.hpp"

#include "elf_loader.hpp"
#include "elf_segment_loader.hpp"
#include "legacy_isa_enum.hpp"
#include "module/symbol_table.hpp"
#include "pnp_manager.hpp"
#include "stdkrn.hpp"
#include "vmm.hpp"

#include <Kernel.hpp>
#include <kernel/configuration_manager.hpp>
#include <kernel/elf.hpp>
#include <memory.hpp>
#include <new.hpp>
#include <system_error.hpp>

namespace {
kernel::driver::SymbolTable g_SymbolTable;

enum class ElfError {
  Success = 0,
  InvalidHeader,
  BoundsViolation,
  RelocationFailure,
  AllocationFailure
};

// Plain function pointer wrapping g_SymbolTable::Resolve, since cfunc only
// wraps C-style function pointers (no captures).
kstd::expected<uintptr_t, kstd::error_code> ResolveSymbol(kstd::string_view name) {
  return g_SymbolTable.Resolve(name);
}

// Copies a matched ".driver_name" section's bytes into the static_string
// pointed to by `context`, before elf_loader frees its temporary read
// buffer. The name is captured separately since KObject::name is const and
// can only be set once the KDriverObject is actually constructed.
void CaptureDriverName(void *context, kstd::string_view name, const uint8_t *data, size_t) {
  if (name != ".driver_name") return;
  auto *name_out = static_cast<kstd::static_string<64> *>(context);
  *name_out = reinterpret_cast<const char *>(data);
}

// Enumerates every child under LocalMachine's "/Drivers" key and calls
// KE_DRIVER_Load(ImagePath) for each one whose "Start" value matches
// `startFilter` exactly. Shared between the pre-root-mount "Boot" pass
// (kernel::driver::init()) and the post-root-mount "Automatic" pass
// (kernel::driver::LoadAutomaticStartDrivers()).
void LoadDriversWithStart(const kstd::string_view &startFilter, const kstd::string_view &logLabel) {
  KE_Print("Loading {} drivers...", logLabel);

  const auto localMachine = KE_CM_OpenRoot("LocalMachine");
  if (!localMachine) {
    KE_Print("Loading {} drivers: failed to open LocalMachine: {}", logLabel,
             localMachine.error().message());
    KE_Print("Loading {} drivers complete.", logLabel);
    return;
  }

  const auto driversKey = KE_CM_OpenKey(*localMachine, "Drivers");
  if (!driversKey) {
    if (driversKey.error() == kernel::KernelError::NotFound) {
      KE_Print("Loading {} drivers: no Drivers key under LocalMachine", logLabel);
    } else {
      KE_Print("Loading {} drivers: failed to open Drivers key: {}", logLabel,
               driversKey.error().message());
    }
    KE_Print("Loading {} drivers complete.", logLabel);
    return;
  }

  for (uint32_t index = 0;; ++index) {
    const auto childEntry = KE_CM_EnumKey(*driversKey, index);
    if (!childEntry) {
      KE_Print("Loading {} drivers: enumeration failed: {}", logLabel,
               childEntry.error().message());
      break;
    }
    if (!childEntry->available) {
      break;
    }
    const auto childName = childEntry->name;

    if (const auto driverKey = KE_CM_OpenKey(*driversKey, childName)) {
      if (const auto startKey = KE_CM_OpenKey(*driverKey, "Start")) {
        kstd::string startValue;
        const auto startSize = KE_CM_GetKeyValueStringSize(*startKey);
        if (!startSize) {
          KE_Print("{}-start driver '{}' has invalid Start value: {}", logLabel, childName,
                   startSize.error().message());
          continue;
        }
        startValue.resize(*startSize);
        const auto startRead = KE_CM_GetKeyValueString(*startKey, kstd::span(startValue.data(), startValue.size()));
        if (!startRead) {
          KE_Print("{}-start driver '{}' failed to read Start value: {}", logLabel, childName,
                   startRead.error().message());
          continue;
        }
        if (startValue == startFilter) {
          if (const auto imagePathKey = KE_CM_OpenKey(*driverKey, "ImagePath")) {
            kstd::string imagePath;
            const auto imagePathSize = KE_CM_GetKeyValueStringSize(*imagePathKey);
            if (!imagePathSize) {
              KE_Print("{}-start driver '{}' has invalid ImagePath: {}", logLabel, childName,
                       imagePathSize.error().message());
              continue;
            }
            imagePath.resize(*imagePathSize);
            const auto imagePathRead = KE_CM_GetKeyValueString(*imagePathKey, kstd::span(imagePath.data(), imagePath.size()));

            if (imagePathRead &&
                !imagePath.empty()) {
              // Both "Boot"- and "Automatic"-start drivers' ImagePath now
              // resolve unmodified through "/": Boot-start drivers load
              // while BootDeviceVolume is still mounted at "/", Automatic-
              // start drivers load once the real root fs has replaced it.
              KE_Print("Loading {}-start driver '{}' from '{}'", logLabel, childName, imagePath);
              const auto load_result = KE_DRIVER_Load(imagePath);
              if (!load_result) {
                KE_Print("{}-start driver '{}' failed: {}", logLabel, childName,
                         load_result.error().message());
              }
            } else {
              KE_Print("{}-start driver '{}' has empty/invalid ImagePath, skipping", logLabel, childName);
            }
          } else {
            KE_Print("{}-start driver '{}' has no ImagePath, skipping", logLabel, childName);
          }
        }
      }
    }
  }

  KE_Print("Loading {} drivers complete.", logLabel);
}

}// namespace

namespace kernel::driver {
kstd::error_code init(const BootInfoT &boot_info) {
  for (const auto &sym: boot_info.boot_symbols) {
    if (sym.name.empty()) continue;
    if (const auto status = g_SymbolTable.Register(sym.name, static_cast<uintptr_t>(sym.address)))
      return status;
  }
  return {};
}

void LoadBootDrivers() {
  // Load boot-start drivers declared under LocalMachine's /Drivers key
  // (Start == "Boot", ImagePath = VFS path to the driver ELF). These are
  // needed to bring up the real disk stack and mount the root filesystem.
  LoadDriversWithStart("Boot", "boot");
}

void LoadAutomaticStartDrivers() {
  // Drivers that aren't needed to mount the root filesystem (e.g. gfx_vga)
  // are deferred until the real root fs is mounted and LocalMachine has been
  // rebound onto it, so their ImagePath resolves through the writable mount.
  LoadDriversWithStart("Automatic", "automatic");

  // Fixed, always-present legacy ISA hardware (e.g. gfx_vga) isn't discovered
  // by any real bus, so the generic Legacy ISA Reservations enumerator reads
  // LocalMachine\LegacyISAReservations, probes each configured device, and
  // reports it (with resources built straight from the registry) to the PnP
  // manager, which then creates the PDO and binds the matching driver via
  // AddDevice/StartDevice, matching how bus_isa reports its ATA-channel PDOs.
  EnumerateLegacyISADevices();
}

void LoadLastResortDrivers() {
  LoadDriversWithStart("LastResort", "last-resort");
  EnumerateLastResortLegacyISADevices();
}
}// namespace kernel::driver

UNDOS_KERNEL_API_DEF kstd::expected<kernel::KObjectPtr<kernel::KDriverObject>, kstd::error_code>
KE_DRIVER_Load(const kstd::string_view &path) noexcept {
  if (path.empty()) return kstd::unexpected(kernel::KernelError::InvalidArgument);

  kstd::static_string<64> captured_name = "";

  kernel::elf::LoadPolicy policy;
  policy.relocatable = true;
  policy.context = &captured_name;
  policy.resolve_symbol = kernel::cfunc<kstd::expected<uintptr_t, kstd::error_code>(kstd::string_view)>(ResolveSymbol);
  policy.capture_section = kernel::cfunc<void(void *, kstd::string_view, const uint8_t *, size_t)>(CaptureDriverName);
  policy.capture_section_name = ".driver_name";


  const auto result = kernel::elf::LoadElfFromVfs(path, policy);
  if (!result) {
    KE_Print("KE_DRIVER_Load: Failed to load driver from path {}", path);
    return kstd::unexpected(result.error());
  }

  auto driver_object = kernel::CreateKObject<kernel::KDriverObject>(captured_name);
  if (!driver_object) {
    KE_Print("KE_DRIVER_Load: Failed to create driver object");
    return kstd::unexpected(driver_object.error());
  }

  // Give the driver its own configuration key under LocalMachine, e.g.
  // "/LocalMachine/Drivers/<name>". LocalMachine is disk-backed from the
  // start of boot (first via BootDeviceVolume mounted at "/", then rebound
  // onto the real root mount, also at "/"), so there is no separate
  // volatile "Boot" hive to target.
  kernel::KConfigurationKeyPtr driverConfig = nullptr;
  if (const auto driverConfigRoot = KE_CM_OpenRoot("LocalMachine")) {
    if (const auto driversKey = KE_CM_CreateKey(*driverConfigRoot, "Drivers", kernel::KConfigurationKeyAccess::ReadWrite)) {
      const auto driverKey = KE_CM_CreateKey(*driversKey, (*driver_object)->name, kernel::KConfigurationKeyAccess::ReadWrite);
      if (driverKey) {
        driverConfig = *driverKey;
      } else {
        KE_Print("KE_DRIVER_Load: failed to create driver configuration key: {}",
                 driverKey.error().message());
      }
    } else {
      KE_Print("KE_DRIVER_Load: failed to create Drivers configuration key: {}",
               driversKey.error().message());
    }
  } else {
    KE_Print("KE_DRIVER_Load: LocalMachine configuration unavailable: {}",
             driverConfigRoot.error().message());
  }

  driver_object.value()->load_base = result->load_base;
  driver_object.value()->total_size = result->total_size;
  driver_object.value()->entry_point = reinterpret_cast<void (*)(kernel::KObjectPtr<kernel::KDriverObject> &, const kernel::KConfigurationKeyPtr &)>(result->entry_point.value);
  driver_object.value()->entry_point(*driver_object, driverConfig);

  // TODO: Remove the entry point from memory once it's done executing

  const auto status = KE_OB_InsertObject(KE_OB_GetDriverDirectory(), *driver_object);
  if (status) return kstd::unexpected(status);
  const auto pnp_status = KE_PNP_RegisterDriver(*driver_object);
  if (pnp_status) {
    const auto rollback_status = KE_OB_RemoveObject(KE_OB_GetDriverDirectory(), *driver_object);
    if (rollback_status) {
      KE_Print("KE_DRIVER_Load: failed to roll back driver registration: {}",
               rollback_status.message());
    }
    return kstd::unexpected(pnp_status);
  }
  return *driver_object;
}
