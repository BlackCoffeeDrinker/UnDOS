#include <catch2/catch_test_macros.hpp>

#include <Kernel.hpp>
#include <kernel/device.hpp>
#include <kernel/hal_interface.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "ob_test_support.hpp"
#include "pnp_test_hooks.hpp"

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
  unsigned postResetDelayPolls{0};
  unsigned postResetClockAdvanceLimit{2};
  kstd::chrono::nanoseconds postResetClockStart{};
  kstd::chrono::nanoseconds firstPostResetStatusAt{};
  bool postResetStatusSeen{false};
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

  void Reset();
  void ResetState() {
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
    postResetDelayPolls = 0;
    postResetClockAdvanceLimit = 2;
    postResetClockStart = kstd::chrono::nanoseconds{0};
    firstPostResetStatusAt = kstd::chrono::nanoseconds{0};
    postResetStatusSeen = false;
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
    if (resetStatus && !resetAsserted && !postResetStatusSeen) {
      postResetStatusSeen = true;
      firstPostResetStatusAt = KE_TIME_GetSystemTime();
    }
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

// Drive emulator for probe, StartDevice and ATAPI cases. When active it
// replaces the legacy read/write mock above for command-block registers.
enum class EmuDevice {
  Absent,
  Ata,
  Atapi,
};

struct DeviceEmulator {
  bool active{false};
  EmuDevice master{EmuDevice::Absent};
  EmuDevice slave{EmuDevice::Absent};
  uint8_t absentStatus{0xff};
  uint32_t ataSectors{0x1234};
  uint32_t atapiLastLba{99};
  uint32_t atapiBlockLength{2048};
  bool atapiNoMedium{false};
  size_t atapiPhaseBytes{2048};
  int forcedByteCount{-1};
  std::array<uint8_t, 8> wregs{};
  std::vector<uint8_t> commands;
  std::vector<std::array<uint8_t, 12>> cdbs;
  unsigned statusReads{0};
  enum class Phase { Idle, Aborted, Identify, Cdb, Data, CheckCondition } phase{Phase::Idle};
  std::vector<uint8_t> data;
  size_t dataPos{0};
  size_t phaseRemaining{0};
  std::array<uint8_t, 12> cdb{};
  size_t cdbPos{0};
  bool signature{false};
  bool overrun{false};

  void Reset() { *this = DeviceEmulator{}; active = true; }

  EmuDevice Selected() const { return (wregs[6] & 0x10) ? slave : master; }

  static uint8_t AtapiByte(uint32_t lba, size_t byte) {
    return static_cast<uint8_t>(lba * 29 + byte * 3 + (byte >> 8));
  }

  void StartData(std::vector<uint8_t> bytes) {
    data = std::move(bytes);
    dataPos = 0;
    phase = Phase::Data;
    NextPhase();
  }

  void NextPhase() {
    const size_t left = data.size() - dataPos;
    phaseRemaining = forcedByteCount >= 0 ? static_cast<size_t>(forcedByteCount) : std::min(left, atapiPhaseBytes);
  }

  uint8_t Status() {
    ++statusReads;
    if (Selected() == EmuDevice::Absent) return absentStatus;
    switch (phase) {
      case Phase::Idle: return 0x50;
      case Phase::Aborted: return 0x51;
      case Phase::CheckCondition: return 0x51;
      case Phase::Identify:
      case Phase::Cdb:
      case Phase::Data: return 0x58;
    }
    return 0x50;
  }

  uint8_t Read(uint8_t reg) {
    if (reg == 7) return Status();
    if (reg == 1) return phase == Phase::CheckCondition ? 0x24 : (phase == Phase::Aborted ? 0x04 : 0);
    if (Selected() == EmuDevice::Atapi && (signature || phase == Phase::Idle) && phase != Phase::Data) {
      if (reg == 4) return 0x14;
      if (reg == 5) return 0xEB;
    }
    if (phase == Phase::Data) {
      if (reg == 4) return static_cast<uint8_t>(phaseRemaining);
      if (reg == 5) return static_cast<uint8_t>(phaseRemaining >> 8);
    }
    return 0;
  }

  void Command(uint8_t command) {
    commands.push_back(command);
    const auto dev = Selected();
    if (dev == EmuDevice::Absent) return;
    if (command == 0xEC) {
      if (dev == EmuDevice::Atapi) {
        phase = Phase::Aborted;
        signature = true;
        return;
      }
      std::vector<uint8_t> id(512, 0);
      const char serial[] = "SERIAL42            ";
      for (size_t i = 0; i < 20; i += 2) {
        id[20 + i] = static_cast<uint8_t>(serial[i + 1]);
        id[20 + i + 1] = static_cast<uint8_t>(serial[i]);
      }
      id[120] = static_cast<uint8_t>(ataSectors);
      id[121] = static_cast<uint8_t>(ataSectors >> 8);
      id[122] = static_cast<uint8_t>(ataSectors >> 16);
      id[123] = static_cast<uint8_t>(ataSectors >> 24);
      data = std::move(id);
      dataPos = 0;
      phase = Phase::Identify;
    } else if (command == 0xA1 && dev == EmuDevice::Atapi) {
      data.assign(512, 0);
      dataPos = 0;
      phase = Phase::Identify;
    } else if (command == 0xA0 && dev == EmuDevice::Atapi) {
      cdbPos = 0;
      phase = Phase::Cdb;
    } else {
      phase = Phase::Idle;
    }
  }

  void WriteWord(uint16_t value) {
    if (phase != Phase::Cdb) return;
    cdb[cdbPos++] = static_cast<uint8_t>(value);
    cdb[cdbPos++] = static_cast<uint8_t>(value >> 8);
    if (cdbPos < 12) return;
    cdbs.push_back(cdb);
    if (atapiNoMedium) {
      phase = Phase::CheckCondition;
      return;
    }
    if (cdb[0] == 0x25) {
      std::vector<uint8_t> cap(8);
      for (unsigned i = 0; i < 4; ++i) cap[i] = static_cast<uint8_t>(atapiLastLba >> (24 - 8 * i));
      for (unsigned i = 0; i < 4; ++i) cap[4 + i] = static_cast<uint8_t>(atapiBlockLength >> (24 - 8 * i));
      const auto saved = atapiPhaseBytes;
      atapiPhaseBytes = 8;
      StartData(std::move(cap));
      atapiPhaseBytes = saved;
    } else if (cdb[0] == 0x28) {
      const uint32_t lba = (uint32_t{cdb[2]} << 24) | (uint32_t{cdb[3]} << 16) | (uint32_t{cdb[4]} << 8) | cdb[5];
      const uint32_t count = (uint32_t{cdb[7]} << 8) | cdb[8];
      std::vector<uint8_t> bytes(static_cast<size_t>(count) * 2048);
      for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = AtapiByte(lba + static_cast<uint32_t>(i / 2048), i % 2048);
      StartData(std::move(bytes));
    } else {
      phase = Phase::Idle;
    }
  }

  uint16_t ReadWord() {
    if (phase == Phase::Identify) {
      const uint16_t w = static_cast<uint16_t>(data[dataPos] | (data[dataPos + 1] << 8));
      dataPos += 2;
      if (dataPos >= data.size()) phase = Phase::Idle;
      return w;
    }
    if (phase != Phase::Data) return 0;
    if (dataPos + 1 >= data.size()) {
      overrun = true;
      return 0;
    }
    const uint16_t w = static_cast<uint16_t>(data[dataPos] | (data[dataPos + 1] << 8));
    dataPos += 2;
    if (phaseRemaining >= 2) phaseRemaining -= 2;
    if (dataPos >= data.size()) {
      phase = Phase::Idle;
    } else if (phaseRemaining == 0) {
      NextPhase();
    }
    return w;
  }
};

DeviceEmulator g_emu;

void MockAta::Reset() {
  g_emu.active = false;
  ResetState();
}

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

std::array<const kernel::CmPartialResourceDescriptor *, 2> g_assignedPorts{};
const kernel::CmPartialResourceDescriptor *g_assignedIrq{};

UNDOS_HAL_API_DEF void HAL_IO_Out8(uint16_t port, uint8_t value) noexcept {
  if (port == kControlBase) {
    g_mock.resetWrites.push_back(value);
    if (value & 0x04) {
      g_mock.srstAssertedAt = KE_TIME_GetSystemTime();
      g_mock.resetAsserted = true;
      ++g_mock.resetCount;
    } else if (g_mock.resetAsserted) {
      if (g_emu.active) g_emu.phase = DeviceEmulator::Phase::Idle;
      g_mock.srstDeassertedAt = KE_TIME_GetSystemTime();
      test_time::SetSystemTime(g_mock.srstAssertedAt);
      g_mock.postResetClockStart = g_mock.srstAssertedAt;
      g_mock.postResetDelayPolls = 0;
      g_mock.postResetStatusSeen = false;
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
  if (g_emu.active) {
    if (reg == kStatus) g_emu.Command(value);
    else g_emu.wregs[reg] = value;
    return;
  }
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
  if (g_emu.active && port >= kIoBase && port <= kIoBase + kStatus) {
    if (port == kIoBase + kStatus && g_mock.resetStatus && !g_mock.postResetStatusSeen) {
      g_mock.postResetStatusSeen = true;
      g_mock.firstPostResetStatusAt = KE_TIME_GetSystemTime();
    }
    return g_emu.Read(static_cast<uint8_t>(port - kIoBase));
  }
  if (port == kIoBase + kStatus) return g_mock.ReadStatus();
  return 0;
}

UNDOS_HAL_API_DEF void HAL_IO_Out16(uint16_t port, uint16_t value) noexcept {
  if (g_emu.active) {
    if (port == kIoBase + kData) g_emu.WriteWord(value);
    return;
  }
  if (port == kIoBase + kData) g_mock.WriteWord(value);
}

UNDOS_HAL_API_DEF uint16_t HAL_IO_In16(uint16_t port) noexcept {
  if (g_emu.active) return port == kIoBase + kData ? g_emu.ReadWord() : 0;
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
  } else if (g_mock.resetStatus && !g_mock.postResetStatusSeen) {
    // Post-deassert settle wait before the first status poll.
    ++g_mock.postResetDelayPolls;
    if (g_mock.postResetDelayPolls <= g_mock.postResetClockAdvanceLimit) {
      test_time::SetSystemTime(KE_TIME_GetSystemTime() + kMockPitTick);
    }
  }
}

UNDOS_KERNEL_API_DEF const kernel::CmPartialResourceDescriptor *KE_RES_FindAssigned(
    const kernel::KDevicePtr<kernel::KDevice> &device, kernel::ResourceType type, size_t index) noexcept {
  (void) device;
  if (type == kernel::ResourceType::Port && index < g_assignedPorts.size()) return g_assignedPorts[index];
  if (type == kernel::ResourceType::Interrupt && index == 0) return g_assignedIrq;
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
    REQUIRE(g_mock.resetWrites == (std::vector<uint8_t>{0x06, 0x02}));
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
      REQUIRE(g_mock.resetWrites == (std::vector<uint8_t>{0x06, 0x02}));
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
    REQUIRE(g_mock.resetWrites == (std::vector<uint8_t>{0x06, 0x02}));
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
    REQUIRE(g_mock.resetWrites == (std::vector<uint8_t>{0x06, 0x02}));
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

namespace {

kernel::CmPartialResourceDescriptor MakePorts(uint32_t start, uint32_t length) {
  kernel::CmPartialResourceDescriptor d{};
  d.type = kernel::ResourceType::Port;
  d.share = kernel::ResourceShare::DeviceExclusive;
  d.start = start;
  d.length = length;
  return d;
}

struct ChannelFixture {
  kernel::KObjectPtr<kernel::KDriverObject> driver;
  kernel::KDevicePtr<kernel::KDevice> pdo;
  kernel::KDevicePtr<kernel::KDeviceBus> fdo;
  kstd::static_string<32> name;

  ChannelFixture() {
    EnsureObjectManagerInitialized();
    static unsigned counter = 0;
    kstd::format(name, "ATAT{}", counter++);
    driver = kernel::CreateAnonymousKObject<kernel::KDriverObject>().value_or(nullptr);
    pdo = KE_IO_CreateDevice(driver, 0, kstd::string_view{name.data(), name.size()}, kernel::device_type::Bus).value_or(nullptr);
    fdo = KE_IO_CreateBusWithContext<BusContext>(driver).value_or(nullptr);
    pdo->attachedDevice = fdo;
    g_lastReportedPdo = nullptr;
  }

  BusContext *Context() { return fdo->deviceExtension.as<BusContext>(); }

  kstd::static_string<40> DiskName(int position) const {
    kstd::static_string<40> out;
    kstd::format(out, "{}-{}", kstd::string_view{name.data(), name.size()}, position);
    return out;
  }
};

void ResetEmulator() {
  g_mock.Reset();
  g_emu.Reset();
  g_assignedPorts = {};
  g_assignedIrq = nullptr;
}

} // namespace

TEST_CASE("ATA probe rejects a floating or empty bus promptly") {
  for (const uint8_t floating : {uint8_t{0xff}, uint8_t{0x00}}) {
    ResetEmulator();
    g_emu.absentStatus = floating;
    ChannelFixture channel;
    auto *ctx = channel.Context();
    ctx->ioBase = kIoBase;
    ctx->ctrlBase = kControlBase;
    const auto status = Ata_ProbeDevice(channel.pdo, channel.fdo, *ctx, true);
    REQUIRE(status == kernel::KernelError::NotFound);
    REQUIRE(g_emu.commands.empty());
    REQUIRE(g_emu.statusReads <= 8);
    REQUIRE(g_lastReportedPdo == nullptr);
  }
}

TEST_CASE("ATA StartDevice validates the channel resource contract") {
  struct Case {
    uint32_t ioStart, ioLength;
    bool hasControl;
    uint32_t ctrlStart, ctrlLength;
  };
  for (const Case c : {Case{kIoBase, 7, true, kControlBase, 1},
                       Case{kIoBase, 8, false, 0, 0},
                       Case{kIoBase, 8, true, kControlBase, 0},
                       Case{0xfffc, 8, true, kControlBase, 1},
                       Case{kIoBase, 8, true, 0x10000, 1}}) {
    ResetEmulator();
    g_emu.master = EmuDevice::Ata;
    ChannelFixture channel;
    auto io = MakePorts(c.ioStart, c.ioLength);
    auto ctrl = MakePorts(c.ctrlStart, c.ctrlLength);
    g_assignedPorts = {&io, c.hasControl ? &ctrl : nullptr};
    Ata_StartDevice(channel.pdo);
    REQUIRE(channel.Context()->unusable);
    REQUIRE(channel.Context()->ioBase == 0);
    REQUIRE(g_mock.resetWrites.empty());
    REQUIRE(g_emu.commands.empty());
    REQUIRE(g_lastReportedPdo == nullptr);
  }
}

TEST_CASE("ATA StartDevice times the soft reset and names disks by channel") {
  SECTION("master ATA disk") {
    ResetEmulator();
    g_emu.master = EmuDevice::Ata;
    ChannelFixture channel;
    auto io = MakePorts(kIoBase, 8);
    auto ctrl = MakePorts(kControlBase, 1);
    g_assignedPorts = {&io, &ctrl};
    Ata_StartDevice(channel.pdo);

    REQUIRE_FALSE(channel.Context()->unusable);
    REQUIRE(g_mock.resetWrites == (std::vector<uint8_t>{0x06, 0x02}));
    REQUIRE(g_mock.srstDeassertedAt - g_mock.srstAssertedAt >= kMockMinimumSrstPulse);
    REQUIRE(g_mock.postResetStatusSeen);
    REQUIRE(g_mock.firstPostResetStatusAt - g_mock.postResetClockStart >= 2 * kMockPitTick);
    REQUIRE(g_lastReportedPdo != nullptr);
    const auto expected = channel.DiskName(0);
    REQUIRE(g_lastReportedPdo->Name() == kstd::string_view{expected.data(), expected.size()});
    REQUIRE(g_lastReportedPdo->deviceType == kernel::device_type::Disk);
    const auto *disk = g_lastReportedPdo->deviceExtension.as<AtaContext>();
    REQUIRE(disk->serial == "SERIAL42");
    REQUIRE(disk->sectorCount == 0x1234);
    REQUIRE(disk->sectorSize == kAtaSectorSize);
    REQUIRE_FALSE(disk->isAtapi);
    REQUIRE(disk->isMaster);
  }

  SECTION("slave-only channel") {
    ResetEmulator();
    g_emu.slave = EmuDevice::Ata;
    ChannelFixture channel;
    auto io = MakePorts(kIoBase, 8);
    auto ctrl = MakePorts(kControlBase, 1);
    g_assignedPorts = {&io, &ctrl};
    Ata_StartDevice(channel.pdo);
    REQUIRE(g_lastReportedPdo != nullptr);
    const auto expected = channel.DiskName(1);
    REQUIRE(g_lastReportedPdo->Name() == kstd::string_view{expected.data(), expected.size()});
    REQUIRE_FALSE(g_lastReportedPdo->deviceExtension.as<AtaContext>()->isMaster);
  }

  SECTION("a stopped clock poisons the channel before any probe") {
    for (const bool stallAssert : {true, false}) {
      ResetEmulator();
      g_emu.master = EmuDevice::Ata;
      if (stallAssert) g_mock.resetClockAdvanceLimit = 1;
      else g_mock.postResetClockAdvanceLimit = 1;
      ChannelFixture channel;
      auto io = MakePorts(kIoBase, 8);
      auto ctrl = MakePorts(kControlBase, 1);
      g_assignedPorts = {&io, &ctrl};
      Ata_StartDevice(channel.pdo);
      REQUIRE(channel.Context()->unusable);
      REQUIRE(g_mock.resetWrites == (std::vector<uint8_t>{0x06, 0x02}));
      REQUIRE(g_emu.commands.empty());
      REQUIRE(g_lastReportedPdo == nullptr);
    }
  }
}

TEST_CASE("ATAPI probe creates an Optical PDO with READ CAPACITY geometry") {
  SECTION("medium present") {
    ResetEmulator();
    g_emu.master = EmuDevice::Atapi;
    ChannelFixture channel;
    auto io = MakePorts(kIoBase, 8);
    auto ctrl = MakePorts(kControlBase, 1);
    g_assignedPorts = {&io, &ctrl};
    Ata_StartDevice(channel.pdo);
    REQUIRE(g_lastReportedPdo != nullptr);
    REQUIRE(g_lastReportedPdo->deviceType == kernel::device_type::Optical);
    const auto expected = channel.DiskName(0);
    REQUIRE(g_lastReportedPdo->Name() == kstd::string_view{expected.data(), expected.size()});
    const auto *disk = g_lastReportedPdo->deviceExtension.as<AtaContext>();
    REQUIRE(disk->isAtapi);
    REQUIRE(disk->sectorSize == kAtapiSectorSize);
    REQUIRE(disk->sectorCount == 100);
    REQUIRE(std::find(g_emu.commands.begin(), g_emu.commands.end(), 0xA1) != g_emu.commands.end());
    REQUIRE(g_emu.cdbs.size() == 1);
    REQUIRE(g_emu.cdbs[0][0] == 0x25);
  }

  SECTION("no medium keeps the PDO with zero capacity") {
    ResetEmulator();
    g_emu.master = EmuDevice::Atapi;
    g_emu.atapiNoMedium = true;
    ChannelFixture channel;
    auto io = MakePorts(kIoBase, 8);
    auto ctrl = MakePorts(kControlBase, 1);
    g_assignedPorts = {&io, &ctrl};
    Ata_StartDevice(channel.pdo);
    REQUIRE(g_lastReportedPdo != nullptr);
    REQUIRE(g_lastReportedPdo->deviceType == kernel::device_type::Optical);
    REQUIRE(g_lastReportedPdo->deviceExtension.as<AtaContext>()->sectorCount == 0);
    REQUIRE_FALSE(channel.Context()->unusable);
  }

  SECTION("non-2048 block length leaves the device without capacity") {
    ResetEmulator();
    g_emu.master = EmuDevice::Atapi;
    g_emu.atapiBlockLength = 512;
    ChannelFixture channel;
    auto io = MakePorts(kIoBase, 8);
    auto ctrl = MakePorts(kControlBase, 1);
    g_assignedPorts = {&io, &ctrl};
    Ata_StartDevice(channel.pdo);
    REQUIRE(g_lastReportedPdo != nullptr);
    REQUIRE(g_lastReportedPdo->deviceExtension.as<AtaContext>()->sectorCount == 0);
  }
}

namespace {

struct AtapiFixture : AtaFixture {
  explicit AtapiFixture(uint32_t sectors = 100) : AtaFixture(sectors) {
    disk.deviceType = kernel::device_type::Optical;
    auto *ctx = disk.deviceExtension.as<AtaContext>();
    ctx->isAtapi = true;
    ctx->sectorSize = kAtapiSectorSize;
  }
};

void RequireAtapiBytes(const uint8_t *buffer, size_t size, uint64_t offset) {
  for (size_t i = 0; i < size; ++i) {
    const uint64_t position = offset + i;
    CHECK(buffer[i] == DeviceEmulator::AtapiByte(static_cast<uint32_t>(position / kAtapiSectorSize),
                                                 static_cast<size_t>(position % kAtapiSectorSize)));
  }
}

} // namespace

TEST_CASE("ATAPI reads use PACKET READ(10) with 2048-byte sectors") {
  SECTION("multi-sector read is split into batches of at most 32") {
    ResetEmulator();
    g_emu.master = EmuDevice::Atapi;
    AtapiFixture fixture;
    std::vector<uint8_t> output(40 * kAtapiSectorSize);
    size_t transferred = 0;
    const auto status = Ata_Read(fixture.Device(), 3 * kAtapiSectorSize,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);
    REQUIRE_FALSE(status);
    REQUIRE(transferred == output.size());
    REQUIRE(g_emu.cdbs.size() == 2);
    REQUIRE(g_emu.cdbs[0][0] == 0x28);
    REQUIRE(g_emu.cdbs[0][5] == 3);
    REQUIRE(g_emu.cdbs[0][8] == 32);
    REQUIRE(g_emu.cdbs[1][5] == 35);
    REQUIRE(g_emu.cdbs[1][8] == 8);
    REQUIRE(g_emu.wregs[4] == 0x00);
    REQUIRE(g_emu.wregs[5] == 0x08);
    REQUIRE_FALSE(g_emu.overrun);
    RequireAtapiBytes(output.data(), output.size(), 3 * kAtapiSectorSize);
  }

  SECTION("unaligned range preserves surrounding bytes") {
    ResetEmulator();
    g_emu.master = EmuDevice::Atapi;
    AtapiFixture fixture;
    constexpr uint64_t offset = 16 * kAtapiSectorSize + 100;
    constexpr size_t size = 2 * kAtapiSectorSize + 77;
    std::vector<uint8_t> guarded(size + 2, 0xa5);
    size_t transferred = 0;
    const auto status = Ata_Read(fixture.Device(), offset,
        kstd::span<uint8_t>(guarded.data() + 1, size), transferred);
    REQUIRE_FALSE(status);
    REQUIRE(transferred == size);
    REQUIRE(guarded.front() == 0xa5);
    REQUIRE(guarded.back() == 0xa5);
    RequireAtapiBytes(guarded.data() + 1, size, offset);
  }

  SECTION("capacity is enforced and writes are rejected before any I/O") {
    ResetEmulator();
    g_emu.master = EmuDevice::Atapi;
    AtapiFixture fixture(100);
    std::vector<uint8_t> output(2 * kAtapiSectorSize);
    size_t transferred = 0;
    auto status = Ata_Read(fixture.Device(), 99 * kAtapiSectorSize,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);
    REQUIRE(status == kernel::KernelError::OutOfRange);
    REQUIRE(g_emu.commands.empty());

    std::array<uint8_t, 4> data{1, 2, 3, 4};
    transferred = 99;
    status = Ata_Write(fixture.Device(), 0, kstd::span<const uint8_t>(data.data(), data.size()), transferred);
    REQUIRE(status == kernel::KernelError::Unsupported);
    REQUIRE(transferred == 0);
    REQUIRE(g_emu.commands.empty());
  }

  SECTION("an Optical PDO without ATAPI context, or a Disk PDO with one, is rejected") {
    ResetEmulator();
    g_emu.master = EmuDevice::Atapi;
    AtapiFixture fixture;
    fixture.disk.deviceType = kernel::device_type::Disk;
    std::vector<uint8_t> output(kAtapiSectorSize);
    size_t transferred = 0;
    REQUIRE(Ata_Read(fixture.Device(), 0, kstd::span<uint8_t>(output.data(), output.size()), transferred) ==
            kernel::KernelError::Unsupported);
    AtaFixture ata;
    ata.disk.deviceType = kernel::device_type::Optical;
    REQUIRE(Ata_Read(ata.Device(), 0, kstd::span<uint8_t>(output.data(), kAtaSectorSize), transferred) ==
            kernel::KernelError::Unsupported);
    REQUIRE(g_emu.commands.empty());
  }

  SECTION("device byte counts that are too large or misaligned are rejected") {
    for (const int forced : {4096, 1000, 0}) {
      ResetEmulator();
      g_emu.master = EmuDevice::Atapi;
      g_emu.forcedByteCount = forced;
      AtapiFixture fixture;
      std::vector<uint8_t> guarded(kAtapiSectorSize + 1, 0xa5);
      size_t transferred = 99;
      const auto status = Ata_Read(fixture.Device(), 0,
          kstd::span<uint8_t>(guarded.data(), kAtapiSectorSize), transferred);
      REQUIRE(status == kernel::KernelError::DeviceError);
      REQUIRE(transferred == 0);
      REQUIRE(guarded.back() == 0xa5);
      REQUIRE(guarded.front() == 0xa5);
      REQUIRE(g_mock.resetCount == 1);
    }
  }

  SECTION("no medium reports DeviceNotReady") {
    ResetEmulator();
    g_emu.master = EmuDevice::Atapi;
    g_emu.atapiNoMedium = true;
    AtapiFixture fixture;
    std::vector<uint8_t> output(kAtapiSectorSize);
    size_t transferred = 99;
    const auto status = Ata_Read(fixture.Device(), 0,
        kstd::span<uint8_t>(output.data(), output.size()), transferred);
    REQUIRE(status == kernel::KernelError::DeviceNotReady);
    REQUIRE(transferred == 0);
    REQUIRE(g_mock.resetCount == 0);
  }
}
