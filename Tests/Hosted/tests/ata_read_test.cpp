#include <catch2/catch_test_macros.hpp>

#include <Kernel.hpp>
#include <kernel/device.hpp>
#include <kernel/hal_interface.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "ob_test_support.hpp"

#include "../../../Kernel/Drivers/disk_ata/ata.cpp"

namespace test_time {
void SetSystemTime(kstd::chrono::nanoseconds now) noexcept;
}

namespace {
constexpr uint16_t kIoBase = 0x1f0;
constexpr uint16_t kControlBase = 0x3f6;
constexpr uint8_t kStatus = 7;
constexpr uint8_t kData = 0;
constexpr kstd::chrono::nanoseconds kMockPitTick{10'000'000};
constexpr kstd::chrono::nanoseconds kMockMinimumSrstPulse{20'000'000};

struct AtaCommand {
  uint8_t command;
  uint8_t features;
  uint8_t count;
  uint32_t lba;
};

enum class StatusMode {
  Normal,
  Busy,
  Absent,
};

struct MockAta {
  std::array<uint8_t, 8> registers{};
  std::vector<AtaCommand> commands;
  std::vector<uint16_t> writtenWords;
  StatusMode mode{StatusMode::Normal};
  uint8_t terminalStatus{0x50};
  int errorSector{-1};
  uint8_t errorStatus{0x01};
  uint8_t absentStatus{0};
  bool resetCompletes{true};
  bool resetAsserted{false};
  bool resetStatus{false};
  uint8_t postResetStatus{0x50};
  kstd::chrono::nanoseconds srstAssertedAt{};
  kstd::chrono::nanoseconds srstDeassertedAt{};
  bool sectorReady{false};
  bool terminalObserved{false};
  bool protocolViolation{false};
  unsigned resetCount{0};
  unsigned resetDelayPolls{0};
  unsigned resetClockAdvanceLimit{2};
  bool resetTornHighWord{false};
  unsigned readReadyPolls{0};
  unsigned completedReadCommands{0};
  unsigned sectorIndex{0};
  unsigned wordIndex{0};
  unsigned busyPollsRemaining{0};
  uint16_t activeCount{0};
  uint32_t activeLba{0};
  uint8_t activeCommand{0};
  uint8_t idleStatus{0x50};
  std::vector<uint8_t> resetWrites;

  void Reset() {
    registers.fill(0);
    commands.clear();
    writtenWords.clear();
    mode = StatusMode::Normal;
    terminalStatus = 0x50;
    errorSector = -1;
    errorStatus = 0x01;
    absentStatus = 0;
    resetCompletes = true;
    resetAsserted = false;
    resetStatus = false;
    postResetStatus = 0x50;
    srstAssertedAt = kstd::chrono::nanoseconds{0};
    srstDeassertedAt = kstd::chrono::nanoseconds{0};
    sectorReady = false;
    terminalObserved = false;
    protocolViolation = false;
    resetCount = 0;
    resetDelayPolls = 0;
    resetClockAdvanceLimit = 2;
    resetTornHighWord = false;
    readReadyPolls = 0;
    completedReadCommands = 0;
    sectorIndex = 0;
    wordIndex = 0;
    busyPollsRemaining = 1;
    activeCount = 0;
    activeLba = 0;
    activeCommand = 0;
    idleStatus = 0x50;
    resetWrites.clear();
  }

  uint8_t ReadStatus() {
    if (resetStatus) return resetCompletes ? postResetStatus : 0x80;
    if (mode == StatusMode::Busy && activeCommand == 0x20) return 0x80;
    if (mode == StatusMode::Absent && activeCommand == 0x20) return absentStatus;
    if (activeCommand == 0x20) {
      if (errorSector >= 0 && sectorIndex == static_cast<unsigned>(errorSector)) return errorStatus;
      if (sectorIndex < activeCount) {
        if (!sectorReady && busyPollsRemaining) {
          --busyPollsRemaining;
          return 0x80;
        }
        sectorReady = true;
        ++readReadyPolls;
        return 0x48;
      }
      if (!terminalObserved) {
        terminalObserved = true;
        ++completedReadCommands;
      }
      return terminalStatus;
    }
    if (activeCommand == 0x30) return wordIndex < 256 ? 0x48 : 0x50;
    return idleStatus;
  }

  void WriteCommand(uint8_t command) {
    activeCommand = command;
    if (command == 0x20 || command == 0x30) {
      resetStatus = false;
      activeCount = registers[2] ? registers[2] : 256;
      activeLba = registers[3] |
                  (static_cast<uint32_t>(registers[4]) << 8) |
                  (static_cast<uint32_t>(registers[5]) << 16) |
                  (static_cast<uint32_t>(registers[6] & 0x0f) << 24);
      commands.push_back({command, registers[1], registers[2], activeLba});
      sectorIndex = 0;
      wordIndex = 0;
      sectorReady = false;
      terminalObserved = false;
      busyPollsRemaining = 1;
    } else {
      commands.push_back({command, registers[1], registers[2], activeLba});
      idleStatus = 0x50;
      activeCommand = 0;
    }
  }

  uint16_t ReadWord() {
    if (activeCommand == 0x20 && !sectorReady) protocolViolation = true;
    const uint32_t lba = activeLba + sectorIndex;
    const uint8_t low = static_cast<uint8_t>(lba * 17 + wordIndex * 14);
    const uint8_t high = static_cast<uint8_t>(lba * 17 + wordIndex * 14 + 7);
    ++wordIndex;
    if (wordIndex == 256) {
      wordIndex = 0;
      ++sectorIndex;
      sectorReady = false;
      busyPollsRemaining = 1;
    }
    return static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8));
  }

  void WriteWord(uint16_t value) {
    writtenWords.push_back(value);
    ++wordIndex;
  }
};

MockAta g_mock;

uint8_t ExpectedByte(uint32_t lba, size_t byte) {
  return static_cast<uint8_t>(lba * 17 + (byte / 2) * 14 + (byte % 2 ? 7 : 0));
}

struct AtaFixture {
  kernel::KDevice physicalDevice{};
  kernel::KDevice peerPhysicalDevice{};
  kernel::KDeviceBus bus{};
  kernel::KDevice disk{};
  kernel::KDevice peerDisk{};

  explicit AtaFixture(uint32_t sectorCount = 4096, uint16_t controlBase = kControlBase) {
    EnsureObjectManagerInitialized();
    bus.deviceExtension = kernel::DataBuffer::Create<BusContext>();
    auto *busContext = bus.deviceExtension.as<BusContext>();
    busContext->ioBase = kIoBase;
    busContext->ctrlBase = controlBase;

    physicalDevice.attachedDevice = kernel::KDevicePtr<kernel::KDevice>(&bus);
    disk.deviceType = kernel::device_type::Disk;
    disk.deviceExtension = kernel::DataBuffer::Create<AtaContext>();
    auto *diskContext = disk.deviceExtension.as<AtaContext>();
    diskContext->pdo = kernel::KDevicePtr<kernel::KDevice>(&physicalDevice);
    diskContext->isAtapi = false;
    diskContext->isMaster = true;
    diskContext->sectorCount = sectorCount;
    diskContext->sectorSize = kAtaSectorSize;

    peerPhysicalDevice.attachedDevice = kernel::KDevicePtr<kernel::KDeviceBus>(&bus);
    peerDisk.deviceType = kernel::device_type::Disk;
    peerDisk.deviceExtension = kernel::DataBuffer::Create<AtaContext>();
    auto *peerContext = peerDisk.deviceExtension.as<AtaContext>();
    peerContext->pdo = kernel::KDevicePtr<kernel::KDevice>(&peerPhysicalDevice);
    peerContext->isAtapi = false;
    peerContext->isMaster = false;
    peerContext->sectorCount = sectorCount;
    peerContext->sectorSize = kAtaSectorSize;
  }

  kernel::KDevicePtr<kernel::KDevice> Device() {
    return kernel::KDevicePtr<kernel::KDevice>(&disk);
  }

  kernel::KDevicePtr<kernel::KDevice> PeerDevice() {
    return kernel::KDevicePtr<kernel::KDevice>(&peerDisk);
  }
};

void RequireReadBytes(const std::vector<uint8_t> &buffer, uint64_t offset) {
  for (size_t i = 0; i < buffer.size(); ++i) {
    const uint64_t position = offset + i;
    const auto lba = static_cast<uint32_t>(position / kAtaSectorSize);
    const auto byte = static_cast<size_t>(position % kAtaSectorSize);
    CHECK(buffer[i] == ExpectedByte(lba, byte));
  }
}

} // namespace

UNDOS_HAL_API_DEF void HAL_IO_Out8(uint16_t port, uint8_t value) noexcept {
  if (port == kControlBase) {
    g_mock.resetWrites.push_back(value);
    if (value & 0x04) {
      g_mock.srstAssertedAt = KE_TIME_GetSystemTime();
      g_mock.resetAsserted = true;
      ++g_mock.resetCount;
    } else if (g_mock.resetAsserted) {
      g_mock.srstDeassertedAt = KE_TIME_GetSystemTime();
      test_time::SetSystemTime(g_mock.srstAssertedAt);
      g_mock.resetAsserted = false;
      g_mock.resetStatus = true;
      if (g_mock.resetCompletes) {
        g_mock.mode = StatusMode::Normal;
        g_mock.activeCommand = 0;
      }
    }
    return;
  }
  if (port < kIoBase || port > kIoBase + kStatus) return;
  const auto reg = static_cast<uint8_t>(port - kIoBase);
  if (reg == kStatus) {
    g_mock.WriteCommand(value);
  } else {
    if (reg == 6 && g_mock.activeCommand == 0x20 &&
        g_mock.sectorIndex == g_mock.activeCount &&
        !g_mock.terminalObserved) {
      g_mock.protocolViolation = true;
    }
    g_mock.registers[reg] = value;
  }
}

UNDOS_HAL_API_DEF uint8_t HAL_IO_In8(uint16_t port) noexcept {
  if (port == kIoBase + kStatus) return g_mock.ReadStatus();
  return 0;
}

UNDOS_HAL_API_DEF void HAL_IO_Out16(uint16_t port, uint16_t value) noexcept {
  if (port == kIoBase + kData) g_mock.WriteWord(value);
}

UNDOS_HAL_API_DEF uint16_t HAL_IO_In16(uint16_t port) noexcept {
  return port == kIoBase + kData ? g_mock.ReadWord() : 0;
}

UNDOS_HAL_API_DEF void HAL_IO_Delay() noexcept {
  if (g_mock.resetAsserted) {
    ++g_mock.resetDelayPolls;
    if (g_mock.resetTornHighWord) {
      // Poll 1: torn sample with only the high word advanced. Poll 2: the
      // completed value after the single real tick. No further ticks.
      constexpr int64_t kHighWordStep = int64_t{1} << 32;
      const auto now = KE_TIME_GetSystemTime();
      if (g_mock.resetDelayPolls == 1) {
        test_time::SetSystemTime(now + kstd::chrono::nanoseconds(kHighWordStep));
      } else if (g_mock.resetDelayPolls == 2) {
        test_time::SetSystemTime(now + kMockPitTick);
      }
      return;
    }
    if (g_mock.resetDelayPolls <= g_mock.resetClockAdvanceLimit) {
      test_time::SetSystemTime(KE_TIME_GetSystemTime() + kMockPitTick);
    }
  }
}

UNDOS_KERNEL_API_DEF const kernel::CmPartialResourceDescriptor *KE_RES_FindAssigned(
    const kernel::KDevicePtr<kernel::KDevice> &device, kernel::ResourceType type, size_t index) noexcept {
  (void) device;
  (void) type;
  (void) index;
  return nullptr;
}

TEST_CASE("ATA multi-sector reads") {
  SECTION("batches 1, 2, 255, 256, and 257 sectors") {
    for (const size_t sectors : {1u, 2u, 255u, 256u, 257u}) {
      g_mock.Reset();
      AtaFixture fixture;
      std::vector<uint8_t> output(sectors * kAtaSectorSize);
      size_t transferred = 0;
      const auto status = Ata_Read(fixture.Device(), 7 * kAtaSectorSize,
          kstd::span<uint8_t>(output.data(), output.size()), transferred);

      REQUIRE_FALSE(status);
      REQUIRE(transferred == output.size());
      RequireReadBytes(output, 7 * kAtaSectorSize);
      const size_t expectedCommands = sectors == 257 ? 2 : 1;
      REQUIRE(g_mock.commands.size() == expectedCommands);
      REQUIRE(g_mock.commands[0].command == 0x20);
      REQUIRE(g_mock.commands[0].features == 0);
      REQUIRE(g_mock.commands[0].lba == 7);
      REQUIRE(g_mock.commands[0].count == (sectors >= 256 ? 0 : sectors));
      REQUIRE(g_mock.readReadyPolls == sectors);
      REQUIRE(g_mock.completedReadCommands == expectedCommands);
      REQUIRE_FALSE(g_mock.protocolViolation);
      if (sectors == 257) {
        REQUIRE(g_mock.commands[1].command == 0x20);
        REQUIRE(g_mock.commands[1].features == 0);
        REQUIRE(g_mock.commands[1].lba == 263);
        REQUIRE(g_mock.commands[1].count == 1);
      }
    }
  }

  SECTION("unaligned byte range and destination preserve surrounding bytes") {
    g_mock.Reset();
    AtaFixture fixture;
    constexpr uint64_t offset = 19 * kAtaSectorSize + 13;
    constexpr size_t size = 1024 + 31;
    std::vector<uint8_t> guarded(size + 3, 0xa5);
    const size_t outputOffset =
        reinterpret_cast<uintptr_t>(guarded.data()) % alignof(uint16_t) == 0 ? 1 : 2;
    size_t transferred = 0;

    const auto status = Ata_Read(fixture.Device(), offset,
        kstd::span<uint8_t>(guarded.data() + outputOffset, size), transferred);

    REQUIRE_FALSE(status);
    REQUIRE(transferred == size);
    REQUIRE(reinterpret_cast<uintptr_t>(guarded.data() + outputOffset) % alignof(uint16_t) != 0);
    REQUIRE(guarded[outputOffset - 1] == 0xa5);
    REQUIRE(guarded[outputOffset + size] == 0xa5);
    REQUIRE_FALSE(g_mock.protocolViolation);
    REQUIRE(g_mock.readReadyPolls == 3);
    REQUIRE(g_mock.completedReadCommands == 3);
    RequireReadBytes(std::vector<uint8_t>(guarded.data() + outputOffset,
        guarded.data() + outputOffset + size), offset);

    g_mock.Reset();
    constexpr size_t fullReadSize = 2 * kAtaSectorSize;
    std::vector<uint8_t> unalignedFullRead(fullReadSize + 3, 0xa5);
    const size_t fullReadOffset =
        reinterpret_cast<uintptr_t>(unalignedFullRead.data()) % alignof(uint16_t) == 0 ? 1 : 2;
    transferred = 0;
    const auto fullReadStatus = Ata_Read(fixture.Device(), 12 * kAtaSectorSize,
        kstd::span<uint8_t>(unalignedFullRead.data() + fullReadOffset, fullReadSize), transferred);
    REQUIRE_FALSE(fullReadStatus);
    REQUIRE(transferred == fullReadSize);
    REQUIRE(reinterpret_cast<uintptr_t>(unalignedFullRead.data() + fullReadOffset) %
        alignof(uint16_t) != 0);
    REQUIRE(unalignedFullRead[fullReadOffset - 1] == 0xa5);
    REQUIRE(unalignedFullRead[fullReadOffset + fullReadSize] == 0xa5);
    REQUIRE_FALSE(g_mock.protocolViolation);
    RequireReadBytes(std::vector<uint8_t>(unalignedFullRead.data() + fullReadOffset,
        unalignedFullRead.data() + fullReadOffset + fullReadSize), 12 * kAtaSectorSize);
  }

  SECTION("capacity boundaries, highest LBA28, and zero-length reads") {
    g_mock.Reset();
    AtaFixture smallDisk(100);
    std::array<uint8_t, kAtaSectorSize> sector{};
    size_t transferred = 0;
    auto status = Ata_Read(smallDisk.Device(), 99 * kAtaSectorSize,
        kstd::span<uint8_t>(sector.data(), sector.size()), transferred);
    REQUIRE_FALSE(status);
    REQUIRE(g_mock.commands.size() == 1);
    REQUIRE(g_mock.commands[0].lba == 99);
    REQUIRE_FALSE(g_mock.protocolViolation);

    g_mock.Reset();
    std::array<uint8_t, 2 * kAtaSectorSize> twoSectors{};
    transferred = 0;
    status = Ata_Read(smallDisk.Device(), 99 * kAtaSectorSize,
        kstd::span<uint8_t>(twoSectors.data(), twoSectors.size()), transferred);
    REQUIRE(status == kernel::KernelError::OutOfRange);
    REQUIRE(transferred == 0);
    REQUIRE(g_mock.commands.empty());

    g_mock.Reset();
    transferred = 0;
    status = Ata_Read(smallDisk.Device(), 0, kstd::span<uint8_t>(), transferred);
    REQUIRE_FALSE(status);
    REQUIRE(transferred == 0);
    REQUIRE(g_mock.commands.empty());
    status = Ata_Read(smallDisk.Device(), 101 * kAtaSectorSize, kstd::span<uint8_t>(), transferred);
    REQUIRE(status == kernel::KernelError::OutOfRange);
    REQUIRE(transferred == 0);
    REQUIRE(g_mock.commands.empty());

    g_mock.Reset();
    AtaFixture lba28Disk(0xffffffffu);
    transferred = 0;
    status = Ata_Read(lba28Disk.Device(), (0x0ffffffeu * static_cast<uint64_t>(kAtaSectorSize)),
        kstd::span<uint8_t>(twoSectors.data(), twoSectors.size()), transferred);
    REQUIRE_FALSE(status);
    REQUIRE(transferred == twoSectors.size());
    REQUIRE(g_mock.commands.size() == 1);
    REQUIRE(g_mock.commands[0].lba == 0x0ffffffeu);
    REQUIRE(g_mock.commands[0].count == 2);
    REQUIRE(g_mock.commands[0].lba + 1 == 0x0fffffffu);
    REQUIRE_FALSE(g_mock.protocolViolation);
  }

  SECTION("mid-batch and terminal errors preserve transferred counts") {
    g_mock.Reset();
    AtaFixture fixture;
    g_mock.errorSector = 1;
    std::array<uint8_t, 3 * kAtaSectorSize> output{};
    size_t transferred = 0;

    auto status = Ata_Read(fixture.Device(), 0,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);
    REQUIRE(status == kernel::KernelError::DeviceError);
    REQUIRE(transferred == kAtaSectorSize);
    REQUIRE(g_mock.commands.size() == 1);
    REQUIRE(g_mock.resetCount == 0);
    REQUIRE_FALSE(g_mock.protocolViolation);
    RequireReadBytes(std::vector<uint8_t>(output.begin(), output.begin() + kAtaSectorSize), 0);

    g_mock.Reset();
    AtaFixture terminalFixture;
    g_mock.terminalStatus = 0x70;
    std::array<uint8_t, kAtaSectorSize> oneSector{};
    transferred = 0;
    status = Ata_Read(terminalFixture.Device(), 0,
        kstd::span<uint8_t>(oneSector.data(), oneSector.size()), transferred);
    REQUIRE(status == kernel::KernelError::DeviceError);
    REQUIRE(transferred == oneSector.size());
    REQUIRE(g_mock.resetCount == 0);
  }

  SECTION("timeout recovery and poisoned-channel rejection") {
    g_mock.Reset();
    AtaFixture recoverable;
    g_mock.mode = StatusMode::Busy;
    std::array<uint8_t, kAtaSectorSize> output{};
    size_t transferred = 0;
    auto status = Ata_Read(recoverable.Device(), 0,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);
    REQUIRE(status == kernel::KernelError::Timeout);
    REQUIRE(g_mock.resetCount == 1);
    REQUIRE(g_mock.resetWrites == (std::vector<uint8_t>{0x04, 0x00}));
    REQUIRE_FALSE(recoverable.bus.deviceExtension.as<BusContext>()->unusable);
    g_mock.mode = StatusMode::Normal;
    transferred = 0;
    status = Ata_Read(recoverable.PeerDevice(), 0,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);
    REQUIRE_FALSE(status);
    REQUIRE(transferred == output.size());
    REQUIRE(g_mock.commands.size() == 2);
    REQUIRE((g_mock.registers[6] & 0xf0) == 0xf0);
    REQUIRE_FALSE(g_mock.protocolViolation);

    std::array<uint8_t, 1> writeData{0x5a};
    for (const uint8_t absentStatus : {uint8_t{0}, uint8_t{0xff}}) {
      g_mock.Reset();
      AtaFixture noControl(4096, 0);
      g_mock.mode = StatusMode::Absent;
      g_mock.absentStatus = absentStatus;
      transferred = 0;
      status = Ata_Read(noControl.PeerDevice(), 0,
          kstd::span<uint8_t>(output.data(), output.size()), transferred);
      REQUIRE(status == kernel::KernelError::DeviceNotReady);
      REQUIRE(noControl.bus.deviceExtension.as<BusContext>()->unusable);
      REQUIRE(g_mock.resetCount == 0);
      const size_t commandsAfterFailure = g_mock.commands.size();
      transferred = 0;
      status = Ata_Read(noControl.Device(), 0,
          kstd::span<uint8_t>(output.data(), output.size()), transferred);
      REQUIRE(status == kernel::KernelError::DeviceNotReady);
      REQUIRE(g_mock.commands.size() == commandsAfterFailure);
      transferred = 0;
      status = Ata_Write(noControl.PeerDevice(), 0,
          kstd::span<const uint8_t>(writeData.data(), writeData.size()), transferred);
      REQUIRE(status == kernel::KernelError::DeviceNotReady);
      REQUIRE(g_mock.commands.size() == commandsAfterFailure);
    }

    g_mock.Reset();
    AtaFixture failedReset;
    g_mock.mode = StatusMode::Busy;
    g_mock.resetCompletes = false;
    transferred = 0;
    status = Ata_Read(failedReset.PeerDevice(), 0,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);
    REQUIRE(status == kernel::KernelError::Timeout);
    REQUIRE(g_mock.resetCount == 1);
    REQUIRE(failedReset.bus.deviceExtension.as<BusContext>()->unusable);
    const size_t commandsBeforeRejectedWrite = g_mock.commands.size();
    transferred = 0;
    status = Ata_Read(failedReset.Device(), 0,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);
    REQUIRE(status == kernel::KernelError::DeviceNotReady);
    REQUIRE(g_mock.commands.size() == commandsBeforeRejectedWrite);
    transferred = 0;
    status = Ata_Write(failedReset.PeerDevice(), 0,
        kstd::span<const uint8_t>(writeData.data(), writeData.size()), transferred);
    REQUIRE(status == kernel::KernelError::DeviceNotReady);
    REQUIRE(g_mock.commands.size() == commandsBeforeRejectedWrite);

    g_mock.Reset();
    AtaFixture lingeringDrq;
    g_mock.terminalStatus = 0x58;
    transferred = 0;
    status = Ata_Read(lingeringDrq.Device(), 0,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);
    REQUIRE(status == kernel::KernelError::Timeout);
    REQUIRE(transferred == output.size());
    REQUIRE(g_mock.resetCount == 1);
    REQUIRE_FALSE(lingeringDrq.bus.deviceExtension.as<BusContext>()->unusable);
  }

  SECTION("recovery enforces SRST timing and requires a clean post-reset status") {
    std::array<uint8_t, kAtaSectorSize> output{};
    std::array<uint8_t, 1> writeData{0x5a};

    for (const uint8_t postResetStatus : {uint8_t{0x58}, uint8_t{0x51}, uint8_t{0x60}}) {
      g_mock.Reset();
      AtaFixture fixture;
      g_mock.mode = StatusMode::Busy;
      g_mock.postResetStatus = postResetStatus;
      size_t transferred = 0;

      const auto status = Ata_Read(fixture.Device(), 0,
          kstd::span<uint8_t>(output.data(), output.size()), transferred);

      REQUIRE(status == kernel::KernelError::Timeout);
      REQUIRE(transferred == 0);
      REQUIRE(g_mock.resetCount == 1);
      REQUIRE(g_mock.srstDeassertedAt - g_mock.srstAssertedAt >=
          kMockMinimumSrstPulse);
      REQUIRE(fixture.bus.deviceExtension.as<BusContext>()->unusable);
      const size_t commandsAfterRecovery = g_mock.commands.size();

      transferred = 99;
      const auto peerReadStatus = Ata_Read(fixture.PeerDevice(), 0,
          kstd::span<uint8_t>(output.data(), output.size()), transferred);
      REQUIRE(peerReadStatus == kernel::KernelError::DeviceNotReady);
      REQUIRE(transferred == 0);
      REQUIRE(g_mock.commands.size() == commandsAfterRecovery);

      transferred = 99;
      const auto peerWriteStatus = Ata_Write(fixture.PeerDevice(), 0,
          kstd::span<const uint8_t>(writeData.data(), writeData.size()), transferred);
      REQUIRE(peerWriteStatus == kernel::KernelError::DeviceNotReady);
      REQUIRE(transferred == 0);
      REQUIRE(g_mock.commands.size() == commandsAfterRecovery);
    }

    g_mock.Reset();
    AtaFixture dirtyDrq;
    g_mock.terminalStatus = 0x58;
    g_mock.postResetStatus = 0x58;
    size_t transferred = 0;
    const auto status = Ata_Read(dirtyDrq.Device(), 0,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);
    REQUIRE(status == kernel::KernelError::Timeout);
    REQUIRE(transferred == output.size());
    REQUIRE(g_mock.resetCount == 1);
    REQUIRE(g_mock.srstDeassertedAt - g_mock.srstAssertedAt >=
        kMockMinimumSrstPulse);
    REQUIRE(dirtyDrq.bus.deviceExtension.as<BusContext>()->unusable);
    const size_t commandsAfterRecovery = g_mock.commands.size();

    transferred = 99;
    const auto peerReadStatus = Ata_Read(dirtyDrq.PeerDevice(), 0,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);
    REQUIRE(peerReadStatus == kernel::KernelError::DeviceNotReady);
    REQUIRE(transferred == 0);
    REQUIRE(g_mock.commands.size() == commandsAfterRecovery);

    transferred = 99;
    const auto peerWriteStatus = Ata_Write(dirtyDrq.PeerDevice(), 0,
        kstd::span<const uint8_t>(writeData.data(), writeData.size()), transferred);
    REQUIRE(peerWriteStatus == kernel::KernelError::DeviceNotReady);
    REQUIRE(transferred == 0);
    REQUIRE(g_mock.commands.size() == commandsAfterRecovery);
  }

  SECTION("stalled reset clock returns boundedly and poisons the shared channel") {
    for (const unsigned clockAdvances : {0u, 1u}) {
      g_mock.Reset();
      AtaFixture fixture;
      g_mock.mode = StatusMode::Busy;
      g_mock.resetClockAdvanceLimit = clockAdvances;
      std::array<uint8_t, kAtaSectorSize> output{};
      std::array<uint8_t, 1> writeData{0x5a};
      size_t transferred = 99;

      const auto status = Ata_Read(fixture.Device(), 0,
          kstd::span<uint8_t>(output.data(), output.size()), transferred);

      REQUIRE(status == kernel::KernelError::Timeout);
      REQUIRE(transferred == 0);
      REQUIRE(g_mock.resetDelayPolls > 0);
      REQUIRE(g_mock.resetDelayPolls <= 1000000);
      REQUIRE(g_mock.resetCount == 1);
      REQUIRE(g_mock.resetWrites == (std::vector<uint8_t>{0x04, 0x00}));
      REQUIRE_FALSE(g_mock.resetAsserted);
      REQUIRE(g_mock.srstDeassertedAt - g_mock.srstAssertedAt ==
          kMockPitTick * clockAdvances);
      REQUIRE(fixture.bus.deviceExtension.as<BusContext>()->unusable);
      REQUIRE(g_mock.commands.size() == 1);
      REQUIRE(g_mock.commands[0].command == 0x20);

      transferred = 99;
      const auto peerReadStatus = Ata_Read(fixture.PeerDevice(), 0,
          kstd::span<uint8_t>(output.data(), output.size()), transferred);
      REQUIRE(peerReadStatus == kernel::KernelError::DeviceNotReady);
      REQUIRE(transferred == 0);
      REQUIRE(g_mock.commands.size() == 1);

      transferred = 99;
      const auto peerWriteStatus = Ata_Write(fixture.PeerDevice(), 0,
          kstd::span<const uint8_t>(writeData.data(), writeData.size()), transferred);
      REQUIRE(peerWriteStatus == kernel::KernelError::DeviceNotReady);
      REQUIRE(transferred == 0);
      REQUIRE(g_mock.commands.size() == 1);
    }
  }

  SECTION("high-word-only change plus one low-word tick cannot satisfy the pulse") {
    g_mock.Reset();
    AtaFixture fixture;
    g_mock.mode = StatusMode::Busy;
    g_mock.resetTornHighWord = true;
    g_mock.postResetStatus = 0x50;
    test_time::SetSystemTime(kstd::chrono::nanoseconds{
        (int64_t{1} << 32) - (kMockPitTick.count() / 2)});
    std::array<uint8_t, kAtaSectorSize> output{};
    std::array<uint8_t, 1> writeData{0x5a};
    size_t transferred = 99;

    const auto status = Ata_Read(fixture.Device(), 0,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);

    REQUIRE(status == kernel::KernelError::Timeout);
    REQUIRE(transferred == 0);
    REQUIRE(g_mock.resetCount == 1);
    REQUIRE(g_mock.resetDelayPolls == 1000000);
    REQUIRE(g_mock.resetWrites == (std::vector<uint8_t>{0x04, 0x00}));
    REQUIRE_FALSE(g_mock.resetAsserted);
    REQUIRE(fixture.bus.deviceExtension.as<BusContext>()->unusable);
    const size_t commandsAfterRecovery = g_mock.commands.size();

    transferred = 99;
    const auto peerReadStatus = Ata_Read(fixture.PeerDevice(), 0,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);
    REQUIRE(peerReadStatus == kernel::KernelError::DeviceNotReady);
    REQUIRE(transferred == 0);
    REQUIRE(g_mock.commands.size() == commandsAfterRecovery);

    transferred = 99;
    const auto peerWriteStatus = Ata_Write(fixture.PeerDevice(), 0,
        kstd::span<const uint8_t>(writeData.data(), writeData.size()), transferred);
    REQUIRE(peerWriteStatus == kernel::KernelError::DeviceNotReady);
    REQUIRE(transferred == 0);
    REQUIRE(g_mock.commands.size() == commandsAfterRecovery);
  }

  SECTION("two clock ticks across the low-word wrap permit recovery") {
    g_mock.Reset();
    AtaFixture fixture;
    g_mock.mode = StatusMode::Busy;
    test_time::SetSystemTime(kstd::chrono::nanoseconds{
        (int64_t{1} << 32) - (kMockPitTick.count() / 2)});
    std::array<uint8_t, kAtaSectorSize> output{};
    size_t transferred = 99;

    const auto status = Ata_Read(fixture.Device(), 0,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);

    REQUIRE(status == kernel::KernelError::Timeout);
    REQUIRE(transferred == 0);
    REQUIRE(g_mock.resetCount == 1);
    REQUIRE(g_mock.resetDelayPolls == 2);
    REQUIRE(g_mock.resetWrites == (std::vector<uint8_t>{0x04, 0x00}));
    REQUIRE_FALSE(fixture.bus.deviceExtension.as<BusContext>()->unusable);
    REQUIRE(g_mock.srstDeassertedAt - g_mock.srstAssertedAt == 2 * kMockPitTick);

    transferred = 0;
    const auto peerReadStatus = Ata_Read(fixture.PeerDevice(), 0,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);
    REQUIRE_FALSE(peerReadStatus);
    REQUIRE(transferred == output.size());
    REQUIRE(g_mock.commands.size() == 2);
    RequireReadBytes(std::vector<uint8_t>(output.begin(), output.end()), 0);
  }

  SECTION("writes remain single-sector with per-sector flushes and partial read-modify-write") {
    g_mock.Reset();
    AtaFixture fixture;
    std::array<uint8_t, 2 * kAtaSectorSize> fullWrite{};
    fullWrite.fill(0x3c);
    size_t transferred = 0;
    auto status = Ata_Write(fixture.Device(), 4 * kAtaSectorSize,
        kstd::span<const uint8_t>(fullWrite.data(), fullWrite.size()), transferred);
    REQUIRE_FALSE(status);
    REQUIRE(transferred == fullWrite.size());
    REQUIRE(g_mock.commands.size() == 4);
    REQUIRE(g_mock.commands[0].command == 0x30);
    REQUIRE(g_mock.commands[0].lba == 4);
    REQUIRE(g_mock.commands[0].count == 1);
    REQUIRE(g_mock.commands[1].command == 0xe7);
    REQUIRE(g_mock.commands[2].command == 0x30);
    REQUIRE(g_mock.commands[2].lba == 5);
    REQUIRE(g_mock.commands[2].count == 1);
    REQUIRE(g_mock.commands[3].command == 0xe7);
    REQUIRE(g_mock.writtenWords.size() == 512);
    REQUIRE(g_mock.writtenWords.front() == 0x3c3c);
    REQUIRE(g_mock.writtenWords[256] == 0x3c3c);

    g_mock.Reset();
    AtaFixture partialFixture;
    std::array<uint8_t, 7> partial{1, 2, 3, 4, 5, 6, 7};
    transferred = 0;
    status = Ata_Write(partialFixture.Device(), 5 * kAtaSectorSize + 10,
        kstd::span<const uint8_t>(partial.data(), partial.size()), transferred);
    REQUIRE_FALSE(status);
    REQUIRE(transferred == partial.size());
    REQUIRE(g_mock.commands.size() == 3);
    REQUIRE(g_mock.commands[0].command == 0x20);
    REQUIRE(g_mock.commands[0].lba == 5);
    REQUIRE(g_mock.commands[0].count == 1);
    REQUIRE(g_mock.commands[1].command == 0x30);
    REQUIRE(g_mock.commands[1].lba == 5);
    REQUIRE(g_mock.commands[1].count == 1);
    REQUIRE(g_mock.commands[2].command == 0xe7);
    REQUIRE(g_mock.writtenWords.size() == 256);
    REQUIRE(g_mock.writtenWords[0] == static_cast<uint16_t>(ExpectedByte(5, 0) |
        (static_cast<uint16_t>(ExpectedByte(5, 1)) << 8)));
    REQUIRE(g_mock.writtenWords[5] == 0x0201);
    REQUIRE(g_mock.writtenWords[8] == static_cast<uint16_t>(7 |
        (static_cast<uint16_t>(ExpectedByte(5, 17)) << 8)));
  }
}
