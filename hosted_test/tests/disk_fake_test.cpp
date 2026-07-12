#include <catch2/catch_test_macros.hpp>

#include "fake_disk.hpp"
#include "fake_disk_file_backend.hpp"

#include <array.hpp>
#include <static_string.hpp>
#include <strfmt.hpp>
#include <cstdio>

namespace {
kstd::static_string<128> MakeTempImagePath(const char *name) {
  kstd::static_string<128> path;
  kstd::format(path, "/tmp/undos_disk_fake_{}.img", name);
  return path;
}
}// namespace

TEST_CASE("disk_fake file backend basic read/write", "[disk_fake]") {
  const auto path = MakeTempImagePath("basic");
  const kstd::string_view pathView(path);
  diskfake::FileBackingStore backing(pathView);
  REQUIRE(backing.IsOpen());

  REQUIRE(backing.Resize(4096));
  REQUIRE(backing.Size() == 4096);

  kstd::array<uint8_t, 16> writeBuf{};
  for (size_t i = 0; i < writeBuf.size(); i++) writeBuf[i] = static_cast<uint8_t>(i);

  REQUIRE(backing.WriteAt(100, kstd::span<const uint8_t>(writeBuf)));

  kstd::array<uint8_t, 16> readBuf{};
  REQUIRE(backing.ReadAt(100, kstd::span<uint8_t>(readBuf)));

  for (size_t i = 0; i < readBuf.size(); i++) {
    REQUIRE(readBuf[i] == writeBuf[i]);
  }

  std::remove(path.data());
}

TEST_CASE("disk_fake Read/Write dispatch functions honor bounds", "[disk_fake]") {
  const auto path = MakeTempImagePath("dispatch");
  const kstd::string_view pathView(path);
  diskfake::FileBackingStore backing(pathView);
  REQUIRE(backing.IsOpen());
  REQUIRE(backing.Resize(512));

  kernel::KDevice device{};
  device.deviceType = kernel::device_type::Disk;
  // DataBuffer expects a KE_Malloc'd (heap) pointer since it KE_Free's it on
  // destruction, so the device extension must be heap-allocated rather than
  // pointing at a stack variable.
  device.deviceExtension = kernel::DataBuffer::Create<diskfake::FakeDiskContext>();
  device.deviceExtension.as<diskfake::FakeDiskContext>()->backing = &backing;

  // Tests bypass the object manager's reference-counted lifecycle; the raw
  // pointer contract is all the free Read/Write functions need.
  const auto devicePtr = kernel::KDevicePtr<kernel::KDevice>(&device);

  kstd::array<uint8_t, 8> outBuf{};
  size_t transferred = 0;
  const auto status = diskfake::Read(devicePtr, 0, kstd::span<uint8_t>(outBuf), transferred);
  REQUIRE(status == kernel::IoStatus::Success);
  REQUIRE(transferred == outBuf.size());

  size_t writeTransferred = 0;
  const auto writeStatus = diskfake::Write(devicePtr, 1000, kstd::span<const uint8_t>(outBuf), writeTransferred);
  REQUIRE(writeStatus == kernel::IoStatus::OutOfRange);

  std::remove(path.data());
}
