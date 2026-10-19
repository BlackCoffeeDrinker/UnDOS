#pragma once

#include <kernel/__core.hpp>
#include <kernel/device.hpp>
#include <kernel/kobject/KDriverObject.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KVolumeMountObject.hpp>
#include <kernel/kobject/KVttyObject.hpp>
#include <kernel/kobject/ObjectType.hpp>
#include <kernel/tty/tty.hpp>
#include <span.hpp>
#include <memory.hpp>

namespace kernel {
struct KVFSNode;
struct KFilesystemObject;
struct KIOObject;
struct KDispatcherHeader;

struct KIOOps {
  cfunc<IoStatus(KIOObject &, kstd::span<uint8_t>, size_t &)> Read;
  cfunc<IoStatus(KIOObject &, kstd::span<const uint8_t>, size_t &)> Write;
  cfunc<IoStatus(KIOObject &, uint32_t, kstd::span<const uint8_t>, kstd::span<uint8_t>, size_t &)> Control;
  cfunc<IoStatus(KIOObject &)> Close;
};

namespace io {
extern const KIOOps FileOps;
extern const KIOOps TtySinkOps;
extern const KIOOps TtySourceOps;
extern const KIOOps DeviceOps;
} // namespace io

struct KIOObject : KObjectT<KIOObject, 1, TYPE_IO> {
  using KObjectT::KObjectT;

  enum class OpenMode : uint32_t {
    Read      = 0x01,
    Write     = 0x02,
    ReadWrite = Read | Write,
    Append    = 0x04,
    Truncate  = 0x08,
    Create    = 0x10,
    Exclusive = 0x20,
    Directory = 0x40,
  };

  const KIOOps *ops{nullptr};
  // Optional waitable state (WaitForObject); owned by the stream provider.
  KDispatcherHeader *waitable{nullptr};
  KDevicePtr<KDevice> device{nullptr};
  tty::VttyPtr vtty{nullptr};
  kstd::static_string<255> actualPath;
  KObjectPtr<KVolumeMountObject> mountPoint;
  kstd::unique_ptr<KVFSNode> vfsNode;
  uint32_t flags{0};
  bool fileHandleCreated{false};

  const KObjectPtr<KFilesystemObject> &FileSystem() const noexcept { return mountPoint->fileSystem; }

  uint64_t offset{0};
  OpenMode mode{OpenMode::ReadWrite};
  bool isClosed{false};
  bool blockingRead{false};
  bool ownsTtyEndpoint{true};
  void *context{nullptr};

  ~KIOObject() override;

  IoStatus Read(kstd::span<uint8_t> buffer, size_t &transferred);
  IoStatus Write(kstd::span<const uint8_t> buffer, size_t &transferred);
  IoStatus Control(uint32_t code, kstd::span<const uint8_t> input, kstd::span<uint8_t> output, size_t &transferred);
  IoStatus Close();
};

inline KIOObject::OpenMode operator|(KIOObject::OpenMode a, KIOObject::OpenMode b) {
  return static_cast<KIOObject::OpenMode>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline bool operator&(KIOObject::OpenMode a, KIOObject::OpenMode b) {
  return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0;
}

template<>
struct ObjectTypeOf<TYPE_IO> {
  using type = KIOObject;
};

} // namespace kernel

// Factory / adapter functions at top-level

UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KIOObject>,
    KE_IO_CreateTtySinkStream,
    const kernel::tty::VttyPtr &vtty,
    const kstd::string_view &name = {});

UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KIOObject>,
    KE_IO_CreateTtySourceStream,
    const kernel::tty::VttyPtr &vtty,
    const kstd::string_view &name = {});

UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KIOObject>,
    KE_IO_CreateDeviceStream,
    const kernel::KDevicePtr<kernel::KDevice> &device,
    kernel::KIOObject::OpenMode mode = kernel::KIOObject::OpenMode::ReadWrite,
    const kstd::string_view &name = {});

UNDOS_KERNEL_PUBLIC_V1API(
    kernel::KObjectPtr<kernel::KIOObject>,
    KE_IO_FromObject,
    const kernel::KObjectPtr<kernel::KObject> &obj);
