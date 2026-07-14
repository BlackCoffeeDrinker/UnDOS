
#include "boot_modules_vfs.hpp"

namespace {
struct FS_VolumeExtension {
  kernel::KObjectPtr<kernel::KDirectoryObject> rootDirectory;
};

bool InitialVFS_MountVolume(const kernel::KObjectPtr<kernel::KVolumeMountObject> &volume, const kstd::string_view &options) {
  // We can only support InitialVFS volumes
  if (volume->volume->Name() != "InitialVFSVolume") {
    early_print_fmt("Invalid volume type for InitialVFS file handle\r\n");
    return false;
  }

  if (auto directory_object = KE_OB_LookupObjectOfType<kernel::KDirectoryObject>(R"(/System/Initial/BootModules)")) {
    volume->driverExtension = kernel::DataBuffer::Create<FS_VolumeExtension>();
    volume->driverExtension.as<FS_VolumeExtension>()->rootDirectory = kstd::move(directory_object);
  } else {
    return false;
  }

  (void) options;
  return true;
}


struct FS_DriverExtension {
  kernel::KObjectPtr<kernel::KModuleObject> module;
};

bool InitialVFS_GetRootNode(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, kernel::KVFSNode &rootOut) {
  (void) mountPoint;

  rootOut.type = kernel::VFSNodeType::Directory;
  rootOut.fsPrivate = nullptr;
  rootOut.size = 0;

  return true;
}

bool InitialVFS_Lookup(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint,
                       const kernel::KVFSNode &parent,
                       kstd::string_view name,
                       kernel::KVFSNode &out) {
  (void) parent;

  // Lookup file directly
  if (const auto root = mountPoint->driverExtension.as<FS_VolumeExtension>()->rootDirectory) {
    if (auto module = KE_OB_FindDirectChildOfType<kernel::KModuleObject>(root, name)) {

      early_print_fmt("Got module {}\r\n", name);

      out.fsPrivate = kernel::DataBuffer::Create<FS_DriverExtension>();
      auto *ctx = out.fsPrivate.as<FS_DriverExtension>();
      ctx->module = kstd::move(module);
      return true;
    }
  }

  early_print_fmt("Failed to find module %s\r\n", name);
  return false;
}

bool InitialVFS_CreateHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &mountPoint, const kernel::KVFSNode &vnode, kernel::KFileObject::OpenMode mode) {
  (void) mountPoint;

  if (vnode.fsPrivate.as<FS_DriverExtension>()->module == nullptr) {
    early_print_fmt("Invalid module for InitialVFS file handle\r\n");
    return false;
  }

  // We only support read files
  if (mode != kernel::KFileObject::OpenMode::Read) {
    early_print_fmt("Invalid open mode for InitialVFS file handle\r\n");
    return false;
  }

  return true;
}

uint64_t InitialVFS_ReadHandle(const kernel::KObjectPtr<kernel::KVolumeMountObject> &, const kernel::KVFSNode &vnode, uint64_t offset, const kstd::span<uint8_t> &buffer) {
  const auto ctx = vnode.fsPrivate.as<FS_DriverExtension>();

  size_t to_read = buffer.size();
  if (offset + to_read > ctx->module->length) {
    to_read = static_cast<uint32_t>(ctx->module->length - offset);
  }

  __builtin_memcpy(
      buffer.data(),
      (ctx->module->base_physical + static_cast<kernel::PhysicalAddress::type>(offset)).as_ptr<uint8_t>(),
      to_read);

  return to_read;
}

}// namespace

namespace kernel::initial {
void init() {
  const auto volume = KE_IO_CreateDevice(
      nullptr, 0,
      "InitialVFSVolume", device_type::Volume);

  {
    const auto filesystem = KE_VFS_RegisterFilesystemDriver(nullptr,
                                                            "InitialVFS");

    if (!volume || !filesystem) {
      early_print_fmt("Failed to create volume or filesystem driver\r\n");
      return;
    }

    filesystem->MountVolume = InitialVFS_MountVolume;
    filesystem->GetRootNode = InitialVFS_GetRootNode;
    filesystem->Lookup = InitialVFS_Lookup;
    filesystem->CreateHandle = InitialVFS_CreateHandle;
    filesystem->ReadHandle = InitialVFS_ReadHandle;
  }

  early_print_fmt("Mounting InitialVFS\r\n");

  KE_VFS_Mount(volume, "InitialVFS", "//!/InitialVFSVolume", {});

  // Load them all
  if (const auto initial = KE_OB_LookupObjectOfType<KDirectoryObject>("/System/Initial/BootModules")) {
    initial->children.each([&](const KObject *child) {
      kstd::static_string<255> name;
      kstd::format(name, "//!/InitialVFSVolume/{}", child->name);
      KE_DRIVER_Load(name);
    });
  }
}

}// namespace kernel::initial
