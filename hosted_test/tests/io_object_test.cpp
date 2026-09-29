#include <catch2/catch_test_macros.hpp>
#include <kernel/kobject/KIOObject.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/kobject/KVttyObject.hpp>
#include <kernel/device.hpp>
#include <kernel/driver.hpp>
#include <kernel/io.hpp>
#include <kernel/object_manager.hpp>
#include <kernel/vfs.hpp>
#include "ob_test_support.hpp"
#include <string.h>

TEST_CASE("KIOObject TTY stream operations", "[io][kioobject][tty]") {
  EnsureObjectManagerInitialized();
  kernel::tty::VttyPtr vtty(new kernel::tty::KVtty());
  REQUIRE(vtty != nullptr);

  auto sourceStream = KE_IO_CreateTtySourceStream(vtty, "TestSource");
  auto sinkStream = KE_IO_CreateTtySinkStream(vtty, "TestSink");
  REQUIRE(sourceStream != nullptr);
  REQUIRE(sinkStream != nullptr);
  REQUIRE(sourceStream->ops == &kernel::io::TtySourceOps);
  REQUIRE(sinkStream->ops == &kernel::io::TtySinkOps);

  SECTION("Write to sourceStream, read line from sinkStream") {
    const uint8_t input[] = {'H', 'e', 'l', 'l', 'o', '\n'};
    size_t written = 0;
    auto status = sourceStream->Write(kstd::span<const uint8_t>(input, sizeof(input)), written);
    REQUIRE(status == kernel::IoStatus::Success);
    REQUIRE(written == sizeof(input));

    uint8_t readBuf[32] = {0};
    size_t readBytes = 0;
    status = sinkStream->Read(kstd::span<uint8_t>(readBuf, sizeof(readBuf)), readBytes);
    REQUIRE(status == kernel::IoStatus::Success);
    REQUIRE(readBytes == 6);
    REQUIRE(memcmp(readBuf, "Hello\n", 6) == 0);
  }

  SECTION("Write to sinkStream, read echo from sourceStream") {
    // Sink writes to display output
    const uint8_t msg[] = {'O', 'K', '\n'};
    size_t written = 0;
    auto status = sinkStream->Write(kstd::span<const uint8_t>(msg, sizeof(msg)), written);
    REQUIRE(status == kernel::IoStatus::Success);
    REQUIRE(written == 3);

    // Source reads output with NL->CRNL translation: "OK\r\n"
    uint8_t readBuf[32] = {0};
    size_t readBytes = 0;
    status = sourceStream->Read(kstd::span<uint8_t>(readBuf, sizeof(readBuf)), readBytes);
    REQUIRE(status == kernel::IoStatus::Success);
    REQUIRE(readBytes == 4);
    REQUIRE(memcmp(readBuf, "OK\r\n", 4) == 0);
  }

  SECTION("KE_IO_FromObject adapters") {
    auto rawSink = kernel::CreateAnonymousKObject<kernel::KVttySinkObject>();
    rawSink->vtty = vtty;
    auto adaptedSink = KE_IO_FromObject(rawSink);
    REQUIRE(adaptedSink != nullptr);
    REQUIRE(adaptedSink->ops == &kernel::io::TtySinkOps);

    auto rawSource = kernel::CreateAnonymousKObject<kernel::KVttySourceObject>();
    rawSource->vtty = vtty;
    auto adaptedSource = KE_IO_FromObject(rawSource);
    REQUIRE(adaptedSource != nullptr);
    REQUIRE(adaptedSource->ops == &kernel::io::TtySourceOps);

    // Calling KE_IO_FromObject on an existing KIOObject returns it as is
    auto identity = KE_IO_FromObject(sourceStream);
    REQUIRE(identity == sourceStream);
  }

  SECTION("Closing KIOObject closes stream and prevents further I/O") {
    REQUIRE(sourceStream->Close() == kernel::IoStatus::Success);
    REQUIRE(sourceStream->isClosed);

    uint8_t buf[16] = {0};
    size_t transferred = 0;
    REQUIRE(sourceStream->Read(kstd::span<uint8_t>(buf, sizeof(buf)), transferred) == kernel::IoStatus::DeviceError);
    REQUIRE(sourceStream->Write(kstd::span<const uint8_t>(buf, sizeof(buf)), transferred) == kernel::IoStatus::DeviceError);
  }
}

TEST_CASE("KIOObject Device stream dispatch", "[io][kioobject][device]") {
  EnsureObjectManagerInitialized();
  static uint8_t devStorage[64] = {0};
  memset(devStorage, 0xAB, sizeof(devStorage));

  auto driver = kernel::CreateAnonymousKObject<kernel::KDriverObject>();
  REQUIRE(driver != nullptr);

  driver->Read = [](const kernel::KDevicePtr<kernel::KDevice> &, uint64_t offset,
                    kstd::span<uint8_t> buffer, size_t &transferred) -> kernel::IoStatus {
    if (offset >= sizeof(devStorage)) {
      transferred = 0;
      return kernel::IoStatus::OutOfRange;
    }
    const size_t available = sizeof(devStorage) - static_cast<size_t>(offset);
    const size_t toCopy = buffer.size() < available ? buffer.size() : available;
    memcpy(buffer.data(), devStorage + offset, toCopy);
    transferred = toCopy;
    return kernel::IoStatus::Success;
  };

  driver->Write = [](const kernel::KDevicePtr<kernel::KDevice> &, uint64_t offset,
                     kstd::span<const uint8_t> buffer, size_t &transferred) -> kernel::IoStatus {
    if (offset >= sizeof(devStorage)) {
      transferred = 0;
      return kernel::IoStatus::OutOfRange;
    }
    const size_t available = sizeof(devStorage) - static_cast<size_t>(offset);
    const size_t toCopy = buffer.size() < available ? buffer.size() : available;
    memcpy(devStorage + offset, buffer.data(), toCopy);
    transferred = toCopy;
    return kernel::IoStatus::Success;
  };

  auto device = KE_IO_CreateDevice(driver, 0, "TestDev", kernel::device_type::Disk, CreateDeviceFlags::None);
  REQUIRE(device != nullptr);

  auto devStream = KE_IO_CreateDeviceStream(device, kernel::KIOObject::OpenMode::ReadWrite, "TestDevStream");
  REQUIRE(devStream != nullptr);
  REQUIRE(devStream->ops == &kernel::io::DeviceOps);

  // Read initial pattern
  uint8_t readBuf[16] = {0};
  size_t transferred = 0;
  auto status = devStream->Read(kstd::span<uint8_t>(readBuf, sizeof(readBuf)), transferred);
  REQUIRE(status == kernel::IoStatus::Success);
  REQUIRE(transferred == 16);
  REQUIRE(readBuf[0] == 0xAB);
  REQUIRE(devStream->offset == 16);

  // Write new data at current offset (16)
  const uint8_t writeBuf[4] = {1, 2, 3, 4};
  status = devStream->Write(kstd::span<const uint8_t>(writeBuf, sizeof(writeBuf)), transferred);
  REQUIRE(status == kernel::IoStatus::Success);
  REQUIRE(transferred == 4);
  REQUIRE(devStream->offset == 20);
  REQUIRE(devStorage[16] == 1);
  REQUIRE(devStorage[17] == 2);
  REQUIRE(devStorage[18] == 3);
  REQUIRE(devStorage[19] == 4);
}
