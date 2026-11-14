
#include "driver_loader.hpp"

#include "elf_loader.hpp"
#include "elf_segment_loader.hpp"
#include "legacy_isa_enum.hpp"
#include "module/module_registry.hpp"
#include "pnp_manager.hpp"
#include "stdkrn.hpp"
#include "subsystem_loader.hpp"
#include "vmm.hpp"

#include <Kernel.hpp>
#include <kernel/configuration_manager.hpp>
#include <kernel/mutex.hpp>
#include <list.hpp>
#include <mutex.hpp>
#include <kernel/elf.hpp>
#include <memory.hpp>
#include <new.hpp>
#include <system_error.hpp>

namespace {
enum class ElfError {
  Success = 0,
  InvalidHeader,
  BoundsViolation,
  RelocationFailure,
  AllocationFailure
};

// Plain function, since cfunc only wraps C-style function pointers (no captures).
kstd::expected<uintptr_t, kstd::error_code> ResolveSymbol(kstd::string_view name) {
  return kernel::module::Resolve(name);
}

// Copies a matched ".driver_name" section's bytes into the static_string
// pointed to by `context`, before elf_loader frees its temporary read
// buffer. The name is captured separately since KObject::name is const and
// can only be set once the KDriverObject is actually constructed.
void CaptureDriverName(void *context, kstd::string_view name, const uint8_t *data, size_t) {
  if (name != ".driver_name")
    return;
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

// Demand-start state (Start == "Demand"): such drivers are never loaded by a
// start pass, only when PnP finds a device mapped to them through the
// CriticalDeviceDatabase. Requests made before the root filesystem is
// mounted are queued; every service is attempted at most once.
// All loads run on a dedicated demand-loader thread, never on the caller's
// (PnP) thread and never with s_demandLock held, so a driver entry point that
// waits for PnP (KE_PNP_WaitForQuiescence) cannot deadlock against it.
kernel::Mutex s_demandLock;
kstd::list<kstd::string> s_demandPending;
kstd::list<kstd::string> s_demandAttempted;
bool s_rootMounted = false;
bool s_demandBusy = false;
// Auto-reset, sticky: wakes the loader thread when work is queued.
kernel::KDispatcherHeader s_demandWork{};
// Auto-reset, sticky: signalled when the loader goes idle (queue drained).
kernel::KDispatcherHeader s_demandIdle{};
kernel::KObjectPtr<kernel::KThreadObject> s_demandThread;

bool ListContains(const kstd::list<kstd::string> &list, const kstd::string_view &name) {
  for (const auto &entry : list)
    if (kstd::string_view(entry.data(), entry.size()) == name) return true;
  return false;
}

kstd::string MakeString(const kstd::string_view &name) {
  kstd::string s;
  s.resize(name.size());
  for (size_t i = 0; i < name.size(); ++i) s[i] = name[i];
  return s;
}

// Reads /Drivers/<service>/ImagePath. Returns an empty string on any failure.
kstd::string ReadImagePath(const kstd::string_view &service) {
  kstd::string imagePath;
  const auto localMachine = KE_CM_OpenRoot("LocalMachine");
  if (!localMachine) return imagePath;
  const auto driversKey = KE_CM_OpenKey(*localMachine, "Drivers");
  if (!driversKey) return imagePath;
  const auto driverKey = KE_CM_OpenKey(*driversKey, service);
  if (!driverKey) return imagePath;
  const auto imagePathKey = KE_CM_OpenKey(*driverKey, "ImagePath");
  if (!imagePathKey) return imagePath;
  const auto size = KE_CM_GetKeyValueStringSize(*imagePathKey);
  if (!size) return imagePath;
  imagePath.resize(*size);
  if (!KE_CM_GetKeyValueString(*imagePathKey, kstd::span(imagePath.data(), imagePath.size()))) {
    imagePath.resize(0);
    return imagePath;
  }
  while (!imagePath.empty() && imagePath[imagePath.size() - 1] == '\0') imagePath.resize(imagePath.size() - 1);
  return imagePath;
}

// Runs on the demand-loader thread without s_demandLock held. Loading
// registers the driver, whose PnP rescan then re-queues the parked device.
void LoadDemandDriver(const kstd::string_view &service) {
  const auto imagePath = ReadImagePath(service);
  if (imagePath.empty()) {
    KE_Print("Demand-start driver '{}' has no usable ImagePath; not retried", service);
    return;
  }
  KE_Print("Loading demand-start driver '{}' from '{}'", service, imagePath);
  if (const auto loaded = KE_DRIVER_Load(kstd::string_view(imagePath.data(), imagePath.size())); !loaded) {
    KE_Print("Demand-start driver '{}' failed: {}; not retried", service, loaded.error().message());
  }
}

void DemandLoaderThread(void *) {
  while (true) {
    kstd::string service;
    bool hasItem = false;
    {
      kstd::unique_lock guard(s_demandLock);
      if (!s_demandPending.empty()) {
        service = s_demandPending.front();
        s_demandPending.pop_front();
        s_demandAttempted.push_back(service);
        s_demandBusy = true;
        hasItem = true;
      } else {
        s_demandBusy = false;
      }
    }
    if (hasItem) {
      LoadDemandDriver(kstd::string_view(service.data(), service.size()));
      continue;
    }
    KE_EVENT_Signal(s_demandIdle, false);
    if (const auto waitStatus = KE_SCHED_WaitForHeader(s_demandWork, kernel::WAIT_INFINITE); waitStatus) {
      KE_Print("Demand loader wait failed: {}", waitStatus.message());
      KE_SCHED_Yield();
    }
  }
}

}// namespace

namespace kernel::driver {
void init() {
}

void RequestDemandLoad(const kstd::string_view &service) {
  if (service.empty()) return;
  bool wake = false;
  {
    kstd::unique_lock guard(s_demandLock);
    if (ListContains(s_demandPending, service) || ListContains(s_demandAttempted, service)) return;
    s_demandPending.push_back(MakeString(service));
    if (s_rootMounted) {
      wake = true;
    } else {
      KE_Print("Demand-start driver '{}' queued until the root filesystem is mounted", service);
    }
  }
  if (wake) KE_EVENT_Signal(s_demandWork, false);
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

  // Demand loads requested during the Boot pass (e.g. by bus_pci's scan)
  // run now that ImagePath resolves, and before the LastResort pass so a
  // demand-loaded display driver claims the console ahead of gfx_vga.
  // The loads run on the demand-loader thread; this thread only waits for
  // the queue to drain.
  s_demandWork.manualReset = false;
  s_demandWork.signalled = false;
  s_demandIdle.manualReset = false;
  s_demandIdle.signalled = false;
  if (auto worker = KE_SCHED_CreateThread(&DemandLoaderThread, nullptr, kernel::ThreadPriority::Normal); worker) {
    s_demandThread = *worker;
    {
      kstd::unique_lock guard(s_demandLock);
      s_rootMounted = true;
    }
    KE_EVENT_Signal(s_demandWork, false);
    // A finished load only registered the driver; the PnP thread binds the
    // parked device afterwards, and that bind can request further demand
    // loads (e.g. a device behind a newly bound bridge). Alternate between
    // draining the queue and PnP quiescence until both are idle, so the
    // legacy ISA and LastResort passes see every device already started.
    while (true) {
      while (true) {
        {
          kstd::unique_lock guard(s_demandLock);
          if (s_demandPending.empty() && !s_demandBusy) break;
        }
        if (KE_SCHED_WaitForHeader(s_demandIdle, kernel::WAIT_INFINITE)) KE_SCHED_Yield();
      }
      KE_PNP_WaitForQuiescence();
      kstd::unique_lock guard(s_demandLock);
      if (s_demandPending.empty() && !s_demandBusy) break;
    }
  } else {
    // No loader thread: demand loads stay queued and never run; boot goes on.
    KE_Print("Demand loader thread not started: {}; demand-start drivers disabled", worker.error().message());
  }

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
  if (path.empty())
    return kstd::unexpected(kernel::KernelError::InvalidArgument);

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
  if (const auto init_status = kernel::elf::RunInitializers(*result)) {
    KE_Print("KE_DRIVER_Load: '{}' has invalid constructor tables: {}", path, init_status.message());
    return kstd::unexpected(init_status);
  }
  driver_object.value()->entry_point(*driver_object, driverConfig);

  // TODO: Remove the entry point from memory once it's done executing

  if (const auto status = KE_OB_InsertObject(KE_OB_GetDriverDirectory(), *driver_object))
    return kstd::unexpected(status);

  if (const auto pnp_status = KE_PNP_RegisterDriver(*driver_object)) {
    if (const auto rollback_status = KE_OB_RemoveObject(KE_OB_GetDriverDirectory(), *driver_object)) {
      KE_Print("KE_DRIVER_Load: failed to roll back driver registration: {}",
               rollback_status.message());
    }
    return kstd::unexpected(pnp_status);
  }

  return *driver_object;
}
