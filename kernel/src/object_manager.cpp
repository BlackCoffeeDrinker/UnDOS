
#include "object_manager.hpp"
#include "stdkrn.hpp"
#include <Kernel.hpp>
#include <kernel/io.hpp>
#include <new.hpp>

namespace {
kernel::KObjectPtr<kernel::KDirectoryObject> g_root;
}

void ObInit() {
  g_root = kernel::CreateKObject<kernel::KDirectoryObject>();
  if (g_root) g_root->name = "";

  if (const auto device = kernel::CreateKObject<kernel::KDirectoryObject>()) {
    device->name = "Device";
    KE_OB_InsertObject(g_root, device);
  }

  if (const auto driver = kernel::CreateKObject<kernel::KDirectoryObject>()) {
    driver->name = "Driver";
    KE_OB_InsertObject(g_root, driver);
  }

  if (const auto memory = kernel::CreateKObject<kernel::KDirectoryObject>()) {
    memory->name = "Memory";
    KE_OB_InsertObject(g_root, memory);
  }

  if (const auto fs = kernel::CreateKObject<kernel::KDirectoryObject>()) {
    fs->name = "FileSystem";
    KE_OB_InsertObject(g_root, fs);
  }

  if (const auto system = kernel::CreateKObject<kernel::KDirectoryObject>()) {
    system->name = "System";
    KE_OB_InsertObject(g_root, system);

    if (const auto initial = kernel::CreateKObject<kernel::KDirectoryObject>()) {
      initial->name = "Initial";
      KE_OB_InsertObject(system, initial);
      if (const auto boot_modules = kernel::CreateKObject<kernel::KDirectoryObject>()) {
        boot_modules->name = "BootModules";
        KE_OB_InsertObject(initial, boot_modules);
      }
    }
  }
}

UNDOS_KERNEL_API bool KE_OB_InsertObject(const kernel::KObjectPtr<kernel::KDirectoryObject>& parent, const kernel::KObjectPtr<kernel::KObject>& child) noexcept {
//UNDOS_KERNEL_API bool KE_OB_InsertObject(kernel::KDirectoryObject *parent, kernel::KObject *child) noexcept {
  if (!parent || !child) return false;

  // Check if name already exists
  if (parent->children.find(kstd::string_view(child->name))) {
    return false;
  }

  child->parent = parent.get();
  child->retain();// The directory holds a reference
  parent->children.insert(*child.get());
  return true;
}

UNDOS_KERNEL_API kernel::KObjectPtr<kernel::KObject> KE_OB_LookupObject(kstd::string_view path) noexcept {
  if (path.empty()) return nullptr;

  kernel::KObject *current = g_root.get();

  if (path[0] == '\\') {
    path = path.substr(1);
  } else {
    return nullptr;
  }

  if (path.empty()) return current;

  while (!path.empty()) {
    const size_t next_slash = path.find('\\');
    kstd::string_view part = (next_slash == kstd::string_view::npos) ? path : path.substr(0, next_slash);

    if (current->type != kernel::TYPE_DIRECTORY) {
      return nullptr;
    }

    const kernel::KDirectoryObject *dir = static_cast<kernel::KDirectoryObject *>(current);
    current = dir->children.find(part);

    if (!current) {
      return nullptr;
    }

    if (next_slash == kstd::string_view::npos) {
      break;
    }
    path = path.substr(next_slash + 1);
  }

  return current;
}

UNDOS_KERNEL_API kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetRootDirectory() noexcept {
  return g_root;
}

UNDOS_KERNEL_API kernel::KObjectPtr<kernel::KPhysicalDeviceObject> KE_IO_CreateDevice(
  kernel::KObjectPtr<kernel::KDriverObject> driver,
  size_t deviceExtensionSize,
  const kstd::string_view& deviceName,
  kernel::DeviceType deviceType) 
{
  auto pdo = kernel::CreateKObject<kernel::KPhysicalDeviceObject>(driver);
  if (!pdo) return nullptr;

  pdo->name = deviceName;
  pdo->deviceType = deviceType;
  if (deviceExtensionSize > 0) {
    pdo->deviceExtension = kernel::data_buffer(KE_Malloc(deviceExtensionSize), deviceExtensionSize);
  }
  // Insert into \Device directory
  if (const auto devDir = KE_OB_LookupObject("\\Device")) {
    KE_OB_InsertObject(devDir.As<kernel::KDirectoryObject>(), pdo);
  }
  return pdo;
}
