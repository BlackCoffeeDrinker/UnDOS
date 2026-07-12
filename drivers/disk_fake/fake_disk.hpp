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

  [[nodiscard]] virtual size_t Size() const = 0;
  virtual bool Resize(size_t newSize) = 0;
  virtual bool ReadAt(uint64_t offset, kstd::span<uint8_t> buffer) = 0;
  virtual bool WriteAt(uint64_t offset, kstd::span<const uint8_t> buffer) = 0;
};

// Device extension for a disk_fake PDO. `backing` is owned externally (by the
// test, or by fake_disk.cpp's in-memory backend on the real target) and must
// outlive the device.
struct FakeDiskContext {
  IBackingStore *backing = nullptr;
  uint32_t sectorSize = 512;
};

// Pre-loads `content` into the backing store, growing it if necessary.
inline bool Seed(const FakeDiskContext &context, kstd::span<const uint8_t> content) {
  if (!context.backing) return false;
  if (context.backing->Size() < content.size() && !context.backing->Resize(content.size())) {
    return false;
  }
  return context.backing->WriteAt(0, content);
}

inline kernel::IoStatus Read(const kernel::KDevicePtr<kernel::KDevice> &device, uint64_t offset, kstd::span<uint8_t> buffer, size_t &transferred) {
  transferred = 0;
  if (device->deviceType != kernel::device_type::Disk) return kernel::IoStatus::Unsupported;

  const auto *ctx = device->deviceExtension.as<FakeDiskContext>();
  if (!ctx || !ctx->backing) return kernel::IoStatus::DeviceError;

  if (offset > ctx->backing->Size() || buffer.size() > ctx->backing->Size() - offset) {
    return kernel::IoStatus::OutOfRange;
  }

  if (!ctx->backing->ReadAt(offset, buffer)) return kernel::IoStatus::DeviceError;

  transferred = buffer.size();
  return kernel::IoStatus::Success;
}

inline kernel::IoStatus Write(const kernel::KDevicePtr<kernel::KDevice> &device, uint64_t offset, kstd::span<const uint8_t> buffer, size_t &transferred) {
  transferred = 0;
  if (device->deviceType != kernel::device_type::Disk) return kernel::IoStatus::Unsupported;

  const auto *ctx = device->deviceExtension.as<FakeDiskContext>();
  if (!ctx || !ctx->backing) return kernel::IoStatus::DeviceError;

  if (offset > ctx->backing->Size() || buffer.size() > ctx->backing->Size() - offset) {
    return kernel::IoStatus::OutOfRange;
  }

  if (!ctx->backing->WriteAt(offset, buffer)) return kernel::IoStatus::DeviceError;

  transferred = buffer.size();
  return kernel::IoStatus::Success;
}

}// namespace diskfake
