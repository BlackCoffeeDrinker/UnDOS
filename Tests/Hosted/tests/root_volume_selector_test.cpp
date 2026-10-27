#include <catch2/catch_test_macros.hpp>

#include <kernel/device.hpp>
#include <kernel/io.hpp>
#include <kernel/kobject/KDeviceObject.hpp>
#include <kernel/kobject/KDirectoryObject.hpp>
#include <kernel/kobject/KDriverObject.hpp>

#include "initial/root_volume_selector.hpp"
#include "ob_test_support.hpp"
#include "stdkrn.hpp"

namespace {

struct ProbeControl {
  uint32_t serial{0x12345678};
  size_t calls{0};
  kstd::string_view matchingDevice{};
  kstd::string_view secondMatchingDevice{};
  kstd::string_view failingDevice{};
};
ProbeControl *g_probeControl = nullptr;

kstd::error_code ReadUnused(const kernel::KDevicePtr<kernel::KDevice> &,
    uint64_t, kstd::span<uint8_t>, size_t &transferred) {
  transferred = 0;
  return {};
}

kernel::KVolumeIdProbeResult ProbeVolume(
    const kernel::KDevicePtr<kernel::KDevice> &device, uint32_t &serial) {
  ++g_probeControl->calls;
  if (device->Name() == g_probeControl->failingDevice)
    return kstd::unexpected(kernel::make_error_code(kernel::KernelError::DeviceError));
  if (device->Name() != g_probeControl->matchingDevice &&
      device->Name() != g_probeControl->secondMatchingDevice)
    return false;
  serial = g_probeControl->serial;
  return true;
}

kernel::KObjectPtr<kernel::KDeviceObject> AddVolume(
    const kernel::KObjectPtr<kernel::KDirectoryObject> &directory,
    kstd::string_view name, bool nativeRead = true,
    kernel::DeviceType type = kernel::device_type::Volume) {
  auto device = KE_CreateKPtrObject<kernel::KDevice>().value_or(nullptr);
  REQUIRE(device != nullptr);
  device->deviceType = type;

  if (nativeRead) {
    auto driver = kernel::CreateKObject<kernel::KDriverObject>("RootSelectorTestDriver").value_or(nullptr);
    REQUIRE(driver != nullptr);
    driver->Read = ReadUnused;
    device->driverObject = driver;
  }

  auto object = kernel::CreateKObject<kernel::KDeviceObject>(name).value_or(nullptr);
  REQUIRE(object != nullptr);
  object->device = device;
  device->deviceObject = object;
  REQUIRE_FALSE(KE_OB_InsertObject(directory, object));
  return object;
}

kernel::KObjectPtr<kernel::KDirectoryObject> MakeDeviceDirectory() {
  return kernel::CreateKObject<kernel::KDirectoryObject>("RootSelectorTestDirectory").value_or(nullptr);
}

}// namespace

TEST_CASE("Root volume UUID parsing and unique selection", "[root-volume]") {
  EnsureObjectManagerInitialized();

  SECTION("Parses the exact eight-digit volume ID and rejects malformed forms") {
    uint32_t id = 0;
    REQUIRE(kernel::root_volume_selector::ParseVolumeId("1234-5678", id));
    REQUIRE(id == 0x12345678);
    REQUIRE(kernel::root_volume_selector::ParseVolumeId("aBcD-Ef09", id));
    REQUIRE(id == 0xabcdef09);

    for (const auto malformed: {"", "12345678", "123-45678", "1234--678",
             "1234-567", "1234-56789", "1234-56g8", "1234-5678 "}) {
      REQUIRE_FALSE(kernel::root_volume_selector::ParseVolumeId(malformed, id));
    }
  }

  SECTION("Skips a synthetic volume and selects the sole matching native volume") {
    const auto directory = MakeDeviceDirectory();
    REQUIRE(directory != nullptr);
    AddVolume(directory, "SyntheticBootVolume", false);
    AddVolume(directory, "UnrelatedNativeDevice", true, kernel::device_type::Disk);
    const auto match = AddVolume(directory, "NativeMatch");
    AddVolume(directory, "NativeNonMatch");

    ProbeControl control{};
    control.matchingDevice = "NativeMatch";
    g_probeControl = &control;
    const auto result = kernel::root_volume_selector::FindVolumeById(
        directory, control.serial, ProbeVolume);

    REQUIRE(result.code == kernel::root_volume_selector::ResultCode::Selected);
    REQUIRE(result.deviceObject == match);
    REQUIRE(result.matchCount == 1);
    REQUIRE(control.calls == 2);
  }

  SECTION("Rejects no matches, ambiguity, and native probe errors") {
    const auto noMatchDirectory = MakeDeviceDirectory();
    REQUIRE(noMatchDirectory != nullptr);
    AddVolume(noMatchDirectory, "NativeVolume");
    ProbeControl noMatch{};
    g_probeControl = &noMatch;
    const auto missing = kernel::root_volume_selector::FindVolumeById(
        noMatchDirectory, 0x12345678, ProbeVolume);
    REQUIRE(missing.code == kernel::root_volume_selector::ResultCode::NoMatch);
    REQUIRE(missing.matchCount == 0);

    const auto ambiguousDirectory = MakeDeviceDirectory();
    REQUIRE(ambiguousDirectory != nullptr);
    AddVolume(ambiguousDirectory, "NativeVolumeA");
    AddVolume(ambiguousDirectory, "NativeVolumeB");
    ProbeControl ambiguous{};
    ambiguous.matchingDevice = "NativeVolumeA";
    ambiguous.secondMatchingDevice = "NativeVolumeB";
    g_probeControl = &ambiguous;
    const auto duplicate = kernel::root_volume_selector::FindVolumeById(
        ambiguousDirectory, 0x12345678, ProbeVolume);
    REQUIRE(duplicate.code == kernel::root_volume_selector::ResultCode::Ambiguous);
    REQUIRE(duplicate.matchCount == 2);
    REQUIRE(duplicate.deviceObject == nullptr);

    const auto errorDirectory = MakeDeviceDirectory();
    REQUIRE(errorDirectory != nullptr);
    AddVolume(errorDirectory, "UnreadableNativeVolume");
    ProbeControl failure{};
    failure.failingDevice = "UnreadableNativeVolume";
    g_probeControl = &failure;
    const auto ioError = kernel::root_volume_selector::FindVolumeById(
        errorDirectory, 0x12345678, ProbeVolume);
    REQUIRE(ioError.code == kernel::root_volume_selector::ResultCode::ProbeError);
    REQUIRE(ioError.failingDeviceName == "UnreadableNativeVolume");
    REQUIRE(ioError.error == kernel::KernelError::DeviceError);
  }
}
