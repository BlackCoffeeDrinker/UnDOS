#pragma once

#include <kernel/device.hpp>
#include <kernel/io.hpp>

#include <span.hpp>

namespace diskfake {

// Abstract backing store for the fake disk. The freestanding i686-elf target
// uses an in-memory store (see fake_disk_memory_backend.hpp), since there is
// no host filesystem available at kernel boot. Hosted tests instead use a
// real image file on disk (see fake_disk_file_backend.hpp) so the resulting
// disk contents can be inspected after a test run.
struct IBackingStore {
  virtual ~IBackingStore() = default;

  [[nodiscard]] virtual kstd::expected<size_t, kernel::KernelError> Size() const = 0;
  virtual kernel::KernelError Resize(size_t newSize) = 0;
  virtual kernel::KernelError ReadAt(uint64_t offset, kstd::span<uint8_t> buffer) = 0;
  virtual kernel::KernelError WriteAt(uint64_t offset, kstd::span<const uint8_t> buffer) = 0;
};

// Device extension for a disk_fake PDO. `backing` is owned externally (by the
// test, or by fake_disk.cpp's in-memory backend on the real target) and must
// outlive the device.
struct FakeDiskContext {
  IBackingStore *backing = nullptr;
  uint32_t sectorSize = 512;
};

// Pre-loads `content` into the backing store, growing it if necessary.
inline kernel::KernelError Seed(const FakeDiskContext &context, kstd::span<const uint8_t> content) {
  if (!context.backing) return kernel::KernelError::InvalidState;
  const auto size = context.backing->Size();
  if (!size) return size.error();
  if (*size < content.size()) {
    const auto status = context.backing->Resize(content.size());
    if (status != kernel::KernelError::Success) return status;
  }
  return context.backing->WriteAt(0, content);
}

inline kernel::KernelError Read(const kernel::KDevicePtr<kernel::KDevice> &device, uint64_t offset, kstd::span<uint8_t> buffer, size_t &transferred) {
  transferred = 0;
  if (!device) return kernel::KernelError::InvalidArgument;
  if (device->deviceType != kernel::device_type::Disk) return kernel::KernelError::Unsupported;

  const auto *ctx = device->deviceExtension.as<FakeDiskContext>();
  if (!ctx || !ctx->backing) return kernel::KernelError::DeviceError;

  const auto size = ctx->backing->Size();
  if (!size) return size.error();
  if (offset > *size || buffer.size() > *size - offset) {
    return kernel::KernelError::OutOfRange;
  }

  if (const auto status = ctx->backing->ReadAt(offset, buffer); status != kernel::KernelError::Success) return status;

  transferred = buffer.size();
  return kernel::KernelError::Success;
}

inline kernel::KernelError Write(const kernel::KDevicePtr<kernel::KDevice> &device, uint64_t offset, kstd::span<const uint8_t> buffer, size_t &transferred) {
  transferred = 0;
  if (!device) return kernel::KernelError::InvalidArgument;
  if (device->deviceType != kernel::device_type::Disk) return kernel::KernelError::Unsupported;

  const auto *ctx = device->deviceExtension.as<FakeDiskContext>();
  if (!ctx || !ctx->backing) return kernel::KernelError::DeviceError;

  const auto size = ctx->backing->Size();
  if (!size) return size.error();
  if (offset > *size || buffer.size() > *size - offset) {
    return kernel::KernelError::OutOfRange;
  }

  if (const auto status = ctx->backing->WriteAt(offset, buffer); status != kernel::KernelError::Success) return status;

  transferred = buffer.size();
  return kernel::KernelError::Success;
}

}// namespace diskfake
