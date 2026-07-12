
#include "object_manager.hpp"
#include "stdkrn.hpp"
#include <Kernel.hpp>
#include <new.hpp>

namespace {
kernel::KObjectPtr<kernel::KDirectoryObject> g_root;
kernel::KObjectPtr<kernel::KDirectoryObject> g_device;
kernel::KObjectPtr<kernel::KDirectoryObject> g_driver;
kernel::KObjectPtr<kernel::KDirectoryObject> g_memory;
kernel::KObjectPtr<kernel::KDirectoryObject> g_filesystem;
kernel::KObjectPtr<kernel::KDirectoryObject> g_thread;
kernel::KObjectPtr<kernel::KDirectoryObject> g_process;
kernel::KObjectPtr<kernel::KDirectoryObject> g_vfs;
kernel::KObjectPtr<kernel::KDirectoryObject> g_file;

kernel::KObjectPtr<kernel::KObject> LookupChild(
    const kernel::KObjectPtr<kernel::KDirectoryObject> &parent,
    const kstd::string_view &path) noexcept {
  if (!parent) return nullptr;
  if (path.empty()) return nullptr;
  if (path == ".") return parent;
  if (path == "..") return parent->parent;

  return parent->children.find(path);
}

kernel::KObjectPtr<kernel::KObject> LookupPathFrom(
    const kernel::KObjectPtr<kernel::KDirectoryObject> &parent,
    const kstd::string_view &path) noexcept {
  if (!parent) return nullptr;
  if (path.empty()) return nullptr;
  if (path == ".") return parent;
  if (path == "..") return parent->parent;

  kernel::KObjectPtr<kernel::KDirectoryObject> current = parent;
  kstd::string_view::size_type pos = 0;

  while (pos != kstd::string_view::npos) {
    const auto next_slash = path.find(kObPathSeperatorChar, pos);
    const auto part = (next_slash == kstd::string_view::npos) ? path.substr(pos) : path.substr(pos, next_slash - pos);
    pos = (next_slash == kstd::string_view::npos) ? kstd::string_view::npos : next_slash + 1;

    // Try and find "part" in current, this might not be a directory!
    if (auto partObj = LookupChild(current, part)) {
      // Are we at the end of the path request? if so just return this
      if (next_slash == kstd::string_view::npos) {
        return partObj;
      }

      // Otherwise, this should be a directory!
      if (partObj->type != kernel::TYPE_DIRECTORY) {
        // Path error
        return nullptr;
      }

      // It's a directory, so continue
      current = partObj.As<kernel::KDirectoryObject>();
    } else {
      // Child wasn't found
      return nullptr;
    }
  }

  return nullptr;
}
}// namespace

namespace kernel::objectmanager {
void init() {
  early_print_fmt("Initializing Object Manager\r\n");

  g_root = kernel::CreateKObject<KDirectoryObject>("<Root>");
  g_device = kernel::CreateKObject<KDirectoryObject>("Device");
  g_driver = kernel::CreateKObject<KDirectoryObject>("Driver");
  g_memory = kernel::CreateKObject<KDirectoryObject>("Memory");
  g_thread = kernel::CreateKObject<KDirectoryObject>("Threads");
  g_process = kernel::CreateKObject<KDirectoryObject>("Processes");
  g_filesystem = kernel::CreateKObject<KDirectoryObject>("FileSystem");
  g_vfs = kernel::CreateKObject<KDirectoryObject>("VFS");
  g_file = kernel::CreateKObject<KDirectoryObject>("File");

  KE_OB_InsertObject(g_root, g_device);
  KE_OB_InsertObject(g_root, g_driver);
  KE_OB_InsertObject(g_root, g_memory);
  KE_OB_InsertObject(g_root, g_filesystem);
  KE_OB_InsertObject(g_root, g_thread);
  KE_OB_InsertObject(g_root, g_process);
  KE_OB_InsertObject(g_root, g_vfs);
  KE_OB_InsertObject(g_root, g_file);

  const auto system = kernel::CreateKObject<KDirectoryObject>("System");
  const auto initial = kernel::CreateKObject<KDirectoryObject>("Initial");
  const auto boot_modules = kernel::CreateKObject<KDirectoryObject>("BootModules");

  KE_OB_InsertObject(g_root, system);
  KE_OB_InsertObject(system, initial);
  KE_OB_InsertObject(initial, boot_modules);

  early_print_fmt("Initial Object Manager started\r\n");
}
}// namespace kernel::objectmanager

UNDOS_KERNEL_API_DEF bool KE_OB_InsertObject(const kernel::KObjectPtr<kernel::KDirectoryObject> &parent, const kernel::KObjectPtr<kernel::KObject> &child) noexcept {
  if (!parent || !child) [[unlikely]] {
    early_print_fmt("KE_OB_InsertObject failed\r\n");
    return false;
  }

  if (child->parent != nullptr) [[unlikely]] {
    // Child already has a parent
    early_print_fmt("KE_OB_InsertObject failed for object {}: Object already has a parent\r\n", child->name);
    return false;
  }

  // Check if name already exists
  if (parent->children.find(kstd::string_view(child->name))) {
    early_print_fmt("KE_OB_InsertObject failed for object {}\r\n", child->name);
    return false;
  }

  child->parent = parent.get();
  KE_OB_Retain(child.get());
  parent->children.insert(*child.get());

  return true;
}

UNDOS_KERNEL_API_DEF bool KE_OB_RemoveObject(const kernel::KObjectPtr<kernel::KDirectoryObject> &parent, const kernel::KObjectPtr<kernel::KObject> &child) noexcept {
  if (!parent || !child) [[unlikely]] {
    return false;
  }

  if (child->parent != parent.get()) [[unlikely]] {
    return false;
  }

  parent->children.remove(child.get());
  child->parent = nullptr;

  // Undo the retain KE_OB_InsertObject performed on insertion.
  KE_OB_Release(child.get());

  return true;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KObject> KE_OB_LookupObject(const kstd::string_view &path) noexcept {
  if (path.empty()) return nullptr;

  if (path[0] == kObPathSeperatorChar) [[likely]] {
    return LookupPathFrom(g_root, path.substr(1));
  }

  return nullptr;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KObject> KE_OB_LookupObjectWithRoot(const kernel::KObjectPtr<kernel::KObject> &root, const kstd::string_view &path) noexcept {
  if (!root) return nullptr;
  if (path.empty()) return root;

  if (root->type != kernel::TYPE_DIRECTORY) [[unlikely]] {
    return nullptr;
  }

  if (path[0] == kObPathSeperatorChar) [[unlikely]] {
    return nullptr;
  }

  return LookupPathFrom(root.As<kernel::KDirectoryObject>(), path);
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KObject> KE_OB_FindDirectChild(const kernel::KObjectPtr<kernel::KDirectoryObject> &parent, const kstd::string_view &name) noexcept {
  return LookupChild(parent, name);
}

UNDOS_KERNEL_API_DEF void KE_OB_Retain(kernel::KObject *obj) noexcept {
  if (!obj) [[unlikely]] {
    return;
  }
  obj->reference_count.fetch_add(1, kstd::memory_order_relaxed);
}

UNDOS_KERNEL_API_DEF void KE_OB_Release(kernel::KObject *obj) noexcept {
  if (!obj) [[unlikely]] {
    return;
  }

  if (obj->reference_count.fetch_sub(1, kstd::memory_order_acq_rel) == 1) {
    kstd::atomic_thread_fence(kstd::memory_order_acquire);
    obj->~KObject();
    KE_Free(obj);
    obj = nullptr;
  }
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetRootDirectory() noexcept {
  return g_root;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetThreadsDirectory() noexcept {
  return g_thread;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetProcessDirectory() noexcept {
  return g_process;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetDriverDirectory() noexcept {
  return g_driver;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetDeviceDirectory() noexcept {
  return g_device;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetMemoryDirectory() noexcept {
  return g_memory;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetVFSDirectory() noexcept {
  return g_vfs;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetFilesystemRoot() noexcept {
  return g_filesystem;
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetFileRoot() noexcept {
  return g_file;
}
