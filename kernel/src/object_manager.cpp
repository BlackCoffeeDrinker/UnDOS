
#include "object_manager.hpp"
#include <Kernel.hpp>
#include <new.hpp>

namespace {
kernel::KObjectPtr<kernel::KDirectoryObject> g_root;
}

void ObInit() {
  g_root = kernel::KE_CreateObject<kernel::KDirectoryObject>();
  if (g_root) g_root->name = "";

  if (const auto device = kernel::KE_CreateObject<kernel::KDirectoryObject>()) {
    device->name = "Device";
    KE_Ob_InsertObject(g_root.get(), device);
  }

  if (const auto driver = kernel::KE_CreateObject<kernel::KDirectoryObject>()) {
    driver->name = "Driver";
    KE_Ob_InsertObject(g_root.get(), driver);
  }

  if (const auto memory = kernel::KE_CreateObject<kernel::KDirectoryObject>()) {
    memory->name = "Memory";
    KE_Ob_InsertObject(g_root.get(), memory);
  }

  if (const auto fs = kernel::KE_CreateObject<kernel::KDirectoryObject>()) {
    fs->name = "FileSystem";
    KE_Ob_InsertObject(g_root.get(), fs);
  }
}

UNDOS_KERNEL_API bool KE_Ob_InsertObject(kernel::KDirectoryObject *parent, kernel::KObject *child) noexcept {
  if (!parent || !child) return false;

  // Check if name already exists
  if (parent->children.find(kstd::string_view(child->name))) {
    return false;
  }

  child->parent = parent;
  child->retain();// The directory holds a reference
  parent->children.insert(child);
  return true;
}

UNDOS_KERNEL_API kernel::KObjectPtr<kernel::KObject> KE_Ob_LookupObject(kstd::string_view path) noexcept {
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

UNDOS_KERNEL_API kernel::KDirectoryObject *KE_Ob_GetRootDirectory() noexcept {
  return g_root.get();
}
