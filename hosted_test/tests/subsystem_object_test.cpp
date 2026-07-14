#include <catch2/catch_test_macros.hpp>

#include <kernel/object_manager.hpp>
#include <kernel/kobject/KProcessObject.hpp>
#include <kernel/kobject/KSubsystemObject.hpp>

#include "object_manager.hpp"
#include "stdkrn.hpp"

#include "ob_test_support.hpp"

TEST_CASE("KSubsystemObject can be created and its fields set/read", "[ob][subsystem]") {
  EnsureObjectManagerInitialized();

  auto subsystem = kernel::CreateKObject<kernel::KSubsystemObject>("SubsystemTest_Basic");
  REQUIRE(subsystem != nullptr);

  subsystem->loader_path = "/System/loader.elf";
  subsystem->loader_entry = kernel::VirtualAddress{0x1000};

  REQUIRE(kstd::string_view(subsystem->loader_path) == "/System/loader.elf");
  REQUIRE(subsystem->loader_entry.value == 0x1000);
  REQUIRE_FALSE(subsystem->systemExtension);
}

TEST_CASE("KSubsystemObject can be registered under and looked up from \\Subsystems", "[ob][subsystem]") {
  EnsureObjectManagerInitialized();
  const auto subsystems = KE_OB_GetSubsystemDirectory();
  REQUIRE(subsystems != nullptr);

  auto subsystem = kernel::CreateKObject<kernel::KSubsystemObject>("SubsystemTest_Lookup");
  const auto *rawPtr = subsystem.get();

  REQUIRE(KE_OB_InsertObject(subsystems, subsystem));

  const auto found = KE_OB_LookupObjectOfTypeWithRoot<kernel::KSubsystemObject>(subsystems, "SubsystemTest_Lookup");
  REQUIRE(found.get() == rawPtr);

  REQUIRE(KE_OB_RemoveObject(subsystems, subsystem));
}

TEST_CASE("Multiple KProcessObjects can share the same long-lived KSubsystemObject", "[ob][subsystem]") {
  EnsureObjectManagerInitialized();
  const auto subsystems = KE_OB_GetSubsystemDirectory();

  auto subsystem = kernel::CreateKObject<kernel::KSubsystemObject>("SubsystemTest_Shared");
  subsystem->loader_path = "/System/loader.elf";
  REQUIRE(KE_OB_InsertObject(subsystems, subsystem));

  auto process1 = kernel::CreateKObject<kernel::KProcessObject>("SubsystemTest_Process1");
  auto process2 = kernel::CreateKObject<kernel::KProcessObject>("SubsystemTest_Process2");
  REQUIRE(process1 != nullptr);
  REQUIRE(process2 != nullptr);

  const auto looked_up = KE_OB_LookupObjectOfTypeWithRoot<kernel::KSubsystemObject>(subsystems, "SubsystemTest_Shared");
  REQUIRE(looked_up != nullptr);

  process1->subsystem = looked_up;
  process2->subsystem = looked_up;

  REQUIRE(process1->subsystem.get() == process2->subsystem.get());
  REQUIRE(process1->subsystem.get() == subsystem.get());

  REQUIRE(KE_OB_RemoveObject(subsystems, subsystem));
}
