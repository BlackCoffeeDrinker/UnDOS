
#include "object_manager.hpp"
#include "stdkrn.hpp"
#include <Kernel.hpp>
#include <new.hpp>

namespace {
kernel::KObjectPtr<kernel::KDirectoryObject> g_root{};
kernel::KObjectPtr<kernel::KDirectoryObject> g_device{};
kernel::KObjectPtr<kernel::KDirectoryObject> g_driver{};
kernel::KObjectPtr<kernel::KDirectoryObject> g_memory{};
kernel::KObjectPtr<kernel::KDirectoryObject> g_filesystem{};
kernel::KObjectPtr<kernel::KDirectoryObject> g_thread{};
kernel::KObjectPtr<kernel::KDirectoryObject> g_process{};
kernel::KObjectPtr<kernel::KDirectoryObject> g_vfs{};
kernel::KObjectPtr<kernel::KDirectoryObject> g_file{};
kernel::KObjectPtr<kernel::KDirectoryObject> g_subsystem{};
kernel::KObjectPtr<kernel::KDirectoryObject> g_configroots{};
kernel::KObjectPtr<kernel::KDirectoryObject> g_gfx{};

kernel::KObjectPtr<kernel::KObject> LookupChild(
    const kernel::KObjectPtr<kernel::KDirectoryObject> &parent,
    const kstd::string_view &path) noexcept {
  if (!parent) return nullptr;
  if (path.empty()) return nullptr;
  if (path == ".") return parent;
  if (path == "..") return parent->parent;

  return parent->child_list.find(path);
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

kernel::KObjectPtr<kernel::KDirectoryObject> EnsureNoError(const kstd::expected<kernel::KObjectPtr<kernel::KDirectoryObject>, kstd::error_code> &expected) {
  if (!expected) {
    KE_Print("Error: {}", expected.error().message());
    HAL_PLATFORM_Panic("Error Fatal OM encountered", __FILE__, __LINE__);
  }

  return *expected;
}

}// namespace

namespace kernel::objectmanager {
bool is_initialized() {
  return g_root != nullptr;
}

void init() {
  auto root = EnsureNoError(kernel::CreateKObject<KDirectoryObject>("<Root>"));
  auto device = EnsureNoError(kernel::CreateKObject<KDirectoryObject>(kOBDevicesPath));
  auto driver = EnsureNoError(kernel::CreateKObject<KDirectoryObject>(kOBDriversPath));
  auto memory = EnsureNoError(kernel::CreateKObject<KDirectoryObject>(kOBMemoryPath));
  auto thread = EnsureNoError(kernel::CreateKObject<KDirectoryObject>(kOBThreadPath));
  auto process = EnsureNoError(kernel::CreateKObject<KDirectoryObject>(kOBProcessesPath));
  auto filesystem = EnsureNoError(kernel::CreateKObject<KDirectoryObject>(kOBFileSystemPath));
  auto vfs = EnsureNoError(kernel::CreateKObject<KDirectoryObject>(kOBVFSPath));
  auto file = EnsureNoError(kernel::CreateKObject<KDirectoryObject>(kOBFilePath));
  auto subsystem = EnsureNoError(kernel::CreateKObject<KDirectoryObject>(kOBSubsystemsPath));
  auto configroots = EnsureNoError(kernel::CreateKObject<KDirectoryObject>(kOBHivePath));
  auto gfx = EnsureNoError(kernel::CreateKObject<KDirectoryObject>(kOBGfxPath));
  auto system = EnsureNoError(kernel::CreateKObject<KDirectoryObject>(kOBSystemPath));

  const KObjectPtr<KObject> children[] = {
      device, driver, memory, filesystem, thread, process, vfs,
      file, subsystem, configroots, gfx, system};

  for (const auto &child: children) {
    if (const auto status = KE_OB_InsertObject(root, child)) {
      KE_Print("Error: {}", status.message());
      HAL_PLATFORM_Panic("Failed to populate the object manager root", __FILE__, __LINE__);
    }
  }

  g_root = kstd::move(root);
  g_device = kstd::move(device);
  g_driver = kstd::move(driver);
  g_memory = kstd::move(memory);
  g_thread = kstd::move(thread);
  g_process = kstd::move(process);
  g_filesystem = kstd::move(filesystem);
  g_vfs = kstd::move(vfs);
  g_file = kstd::move(file);
  g_subsystem = kstd::move(subsystem);
  g_configroots = kstd::move(configroots);
  g_gfx = kstd::move(gfx);
}
}// namespace kernel::objectmanager

UNDOS_KERNEL_API_DEF kstd::error_code KE_OB_InsertObject(const kernel::KObjectPtr<kernel::KDirectoryObject> &parent, const kernel::KObjectPtr<kernel::KObject> &child) noexcept {
  if (!parent || !child) [[unlikely]] {
    KE_Print("KE_OB_InsertObject failed");
    return kernel::KernelError::InvalidArgument;
  }

  if (child->parent != nullptr) [[unlikely]] {
    // Child already has a parent
    KE_Print("KE_OB_InsertObject failed for object {}: Object already has a parent", child->name);
    return kernel::KernelError::AlreadyAttached;
  }

  // Check if name already exists
  if (parent->child_list.find(kstd::string_view(child->name))) {
    KE_Print("KE_OB_InsertObject failed for object {}", child->name);
    return kernel::KernelError::AlreadyExists;
  }

  child->parent = parent.get();
  KE_OB_Retain(child.get());
  parent->child_list.push_back(child.get());

  return kernel::KernelError::Success;
}

UNDOS_KERNEL_API_DEF kstd::expected<uint32_t, kstd::error_code>
KE_OB_MakeUniqueName(const kernel::KObjectPtr<kernel::KDirectoryObject> &parent, const kstd::string_view &baseName) noexcept {
  if (!parent) [[unlikely]] { return kstd::unexpected(kernel::KernelError::InvalidArgument); }
  if (baseName.empty()) [[unlikely]] { return kstd::unexpected(kernel::KernelError::InvalidArgument); }
  for (uint32_t ret = 0;; ++ret) {
    size_t digits = 1;
    for (uint32_t value = ret; value >= 10; value /= 10) ++digits;
    if (baseName.size() + digits > 63) return kstd::unexpected(kernel::KernelError::NameTooLong);
    kstd::static_string<64> candidate;
    kstd::format(candidate, "{}{}", baseName, ret);
    const auto hash = kernel::ObjectNameHash::from_string(candidate);
    bool exists = false;
    for (const auto &child: parent->child_list) {
      if (child->name_hash == hash) {
        exists = true;
        break;
      }
    }
    if (!exists) return ret;
    if (ret == UINT32_MAX) return kstd::unexpected(kernel::KernelError::ResourceLimitExceeded);
  }
}

UNDOS_KERNEL_API_DEF kstd::error_code KE_OB_RemoveObject(const kernel::KObjectPtr<kernel::KDirectoryObject> &parent, const kernel::KObjectPtr<kernel::KObject> &child) noexcept {
  if (!parent || !child) [[unlikely]] {
    return kernel::KernelError::InvalidArgument;
  }

  if (child->parent != parent.get()) [[unlikely]] {
    return kernel::KernelError::ParentMismatch;
  }

  parent->child_list.remove(child.get());
  child->parent = nullptr;

  // Undo the retain KE_OB_InsertObject performed on insertion.
  KE_OB_Release(child.get());

  return kernel::KernelError::Success;
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

  const auto previous = obj->reference_count.fetch_sub(1, kstd::memory_order_acq_rel);
  if (previous == 2 && obj->type == kernel::TYPE_IO && obj->parent == KE_OB_GetFileDirectory().get()) {
    const auto close_status = KE_VFS_CloseFile(static_cast<kernel::KIOObject *>(obj));
    if (close_status) {
      KE_Print("KE_OB_Release: failed to close file object: {}",
               close_status.message());
    }
    return;
  }
  if (previous == 1) {
    kstd::atomic_thread_fence(kstd::memory_order_acquire);
    obj->~KObject();
    KE_Free(obj);
    obj = nullptr;
  }
}

UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetRootDirectory() noexcept { return g_root; }
UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetThreadsDirectory() noexcept { return g_thread; }
UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetProcessDirectory() noexcept { return g_process; }
UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetDriverDirectory() noexcept { return g_driver; }
UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetDeviceDirectory() noexcept { return g_device; }
UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetMemoryDirectory() noexcept { return g_memory; }
UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetVFSDirectory() noexcept { return g_vfs; }
UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetFilesystemDirectory() noexcept { return g_filesystem; }
UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetFileDirectory() noexcept { return g_file; }
UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetSubsystemDirectory() noexcept { return g_subsystem; }
UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetConfigDirectory() noexcept { return g_configroots; }
UNDOS_KERNEL_API_DEF kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetGfxDirectory() noexcept { return g_gfx; }
