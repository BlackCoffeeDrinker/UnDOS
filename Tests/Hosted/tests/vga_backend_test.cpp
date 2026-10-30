#include "gfx_register_mock.hpp"
#define HAL_IO_Out8 gfx_register_mock::Out8
#define HAL_IO_In8 gfx_register_mock::In8
#define KE_RES_FindAssigned gfx_register_mock::FindAssigned
#define KE_RES_ReleaseResources gfx_register_mock::ReleaseResources
#define DriverEntry HostedVgaDriverEntry
#include "../../../Kernel/Drivers/gfx_vga/vga.cpp"
#undef DriverEntry
#undef KE_RES_ReleaseResources
#undef KE_RES_FindAssigned
#undef HAL_IO_In8
#undef HAL_IO_Out8
#include "ob_test_support.hpp"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("VGA real target callbacks implement only the supported planar mode",
          "[gfx][vga][backend]") {
  EnsureObjectManagerInitialized();
  auto device = KE_IO_CreateDeviceWithContext<VgaContext>(
      nullptr, "hosted-vga-device", kernel::device_type::Video);
  REQUIRE(device);
  auto adapter = kernel::CreateAnonymousKObject<kernel::KGraphicsAdapter>().value();
  adapter->device = *device;
  uint8_t framebuffer[65536]{};
  adapter->GetDeviceExtensionAs<VgaContext>()->framebuffer =
      kernel::VirtualAddress::from_ptr(framebuffer);
  auto target = kernel::CreateAnonymousKObject<kernel::KDisplay>().value();
  target->adapter = adapter;
  target->isPresent = true;
  REQUIRE_FALSE(Vga_ConfigureDisplay(adapter, target));
  REQUIRE_FALSE(target->Validate(target));
  const auto mode = target->GetModeInfo(target, 0);
  REQUIRE(mode);
  REQUIRE(mode->width == 640);
  REQUIRE(mode->height == 480);
  REQUIRE(mode->numberOfPlanes == 4);
  REQUIRE(mode->bitsPerPlane == 1);
  REQUIRE_FALSE(target->GetModeInfo(target, 1));
  kernel::KDisplayModeReservation reservation{};
  const auto writes = gfx_register_mock::writes;
  REQUIRE_FALSE(target->PrepareMode(target, 0, reservation));
  REQUIRE(writes == gfx_register_mock::writes);
  REQUIRE_FALSE(target->Disable(target));
  gfx_register_mock::requireBlank = true;
  gfx_register_mock::unblanked = false;
  REQUIRE_FALSE(target->ProgramMode(target, reservation));
  gfx_register_mock::requireBlank = false;
  REQUIRE_FALSE(gfx_register_mock::unblanked);
  REQUIRE_FALSE(target->QueryActualState(target)->enabled);
  REQUIRE_FALSE(target->Enable(target));
  REQUIRE(target->QueryActualState(target)->known);
  REQUIRE(target->QueryActualState(target)->enabled);
  REQUIRE_FALSE(target->Disable(target));
  REQUIRE_FALSE(target->RestoreMode(target, 0, reservation));
  target->FinishMode(target, reservation, true);
  REQUIRE(target->PrepareMode(target, 1, reservation) == kernel::KernelError::OutOfRange);
}
