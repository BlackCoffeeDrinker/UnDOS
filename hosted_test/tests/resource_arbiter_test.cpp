#include <catch2/catch_test_macros.hpp>

#include <kernel/resource.hpp>
#include <kernel/resource_arbiter.hpp>

using namespace kernel;
using kernel::resource::ConflictsWithReserved;
using kernel::resource::RangesOverlap;
using kernel::resource::TryReserveAlternative;

namespace {

IoResourceDescriptor MakeDesc(ResourceType type, uint32_t start, uint32_t length, ResourceShare share = ResourceShare::DeviceExclusive) {
  IoResourceDescriptor d;
  d.type = type;
  d.share = share;
  d.option = ResourceOption::Fixed;
  d.start = start;
  d.end = start + (length ? length - 1 : 0);
  d.length = length;
  d.alignment = 1;
  return d;
}

CmPartialResourceDescriptor MakeCm(ResourceType type, uint32_t start, uint32_t length, ResourceShare share = ResourceShare::DeviceExclusive) {
  CmPartialResourceDescriptor c;
  c.type = type;
  c.share = share;
  c.flags = 0;
  c.start = start;
  c.length = length;
  return c;
}

// Emulates KE_RES_FindAssigned over a granted list: the index-th descriptor of
// the given type, or null.
const CmPartialResourceDescriptor *FindAssigned(const CmResourceList &grant, ResourceType type, size_t index) {
  size_t seen = 0;
  for (auto it = grant.descriptors.begin(); it != grant.descriptors.end(); ++it) {
    if (it->type != type) continue;
    if (seen == index) return &*it;
    ++seen;
  }
  return nullptr;
}

}// namespace

TEST_CASE("RangesOverlap detects intersecting inclusive ranges", "[resource]") {
  REQUIRE(RangesOverlap(0x1F0, 0x1F7, 0x1F4, 0x1FA));// partial overlap
  REQUIRE(RangesOverlap(0, 7, 7, 10));               // touching endpoints
  REQUIRE(RangesOverlap(0, 15, 4, 8));               // fully contained
  REQUIRE_FALSE(RangesOverlap(0, 7, 8, 15));         // adjacent, no overlap
  REQUIRE_FALSE(RangesOverlap(0x1F0, 0x1F7, 0x3F6, 0x3F6));
}

TEST_CASE("A single controller reserves its fixed ranges cleanly", "[resource]") {
  IoResourceList alt;
  IoResourceDescriptor io = MakeDesc(ResourceType::Port, 0x1F0, 8);
  IoResourceDescriptor ctrl = MakeDesc(ResourceType::Port, 0x3F6, 1);
  IoResourceDescriptor irq = MakeDesc(ResourceType::Interrupt, 14, 1);
  alt.descriptors.push_back(&io);
  alt.descriptors.push_back(&ctrl);
  alt.descriptors.push_back(&irq);

  common::DoubleList<CmPartialResourceDescriptor> reserved;// empty

  CmPartialResourceDescriptor pool[4];
  size_t used = 0;
  auto alloc = [&]() -> CmPartialResourceDescriptor * {
    return used < 4 ? &pool[used++] : nullptr;
  };

  CmResourceList grant;
  REQUIRE(TryReserveAlternative(alt, reserved, grant, alloc));
  REQUIRE(grant.descriptors.size() == 3);

  const auto *port0 = FindAssigned(grant, ResourceType::Port, 0);
  const auto *port1 = FindAssigned(grant, ResourceType::Port, 1);
  const auto *intr0 = FindAssigned(grant, ResourceType::Interrupt, 0);
  REQUIRE(port0 != nullptr);
  REQUIRE(port0->start == 0x1F0);
  REQUIRE(port0->length == 8);
  REQUIRE(port1 != nullptr);
  REQUIRE(port1->start == 0x3F6);
  REQUIRE(intr0 != nullptr);
  REQUIRE(intr0->start == 14);
}

TEST_CASE("Two non-overlapping controllers both reserve", "[resource]") {
  // Primary
  IoResourceList primary;
  IoResourceDescriptor pIo = MakeDesc(ResourceType::Port, 0x1F0, 8);
  IoResourceDescriptor pIrq = MakeDesc(ResourceType::Interrupt, 14, 1);
  primary.descriptors.push_back(&pIo);
  primary.descriptors.push_back(&pIrq);

  common::DoubleList<CmPartialResourceDescriptor> reserved;// empty
  CmPartialResourceDescriptor poolA[4];
  size_t usedA = 0;
  auto allocA = [&]() -> CmPartialResourceDescriptor * { return usedA < 4 ? &poolA[usedA++] : nullptr; };
  CmResourceList grantA;
  REQUIRE(TryReserveAlternative(primary, reserved, grantA, allocA));

  // Secondary reserved against the primary's grant.
  IoResourceList secondary;
  IoResourceDescriptor sIo = MakeDesc(ResourceType::Port, 0x170, 8);
  IoResourceDescriptor sIrq = MakeDesc(ResourceType::Interrupt, 15, 1);
  secondary.descriptors.push_back(&sIo);
  secondary.descriptors.push_back(&sIrq);

  CmPartialResourceDescriptor poolB[4];
  size_t usedB = 0;
  auto allocB = [&]() -> CmPartialResourceDescriptor * { return usedB < 4 ? &poolB[usedB++] : nullptr; };
  CmResourceList grantB;
  REQUIRE(TryReserveAlternative(secondary, grantA.descriptors, grantB, allocB));
  REQUIRE(grantB.descriptors.size() == 2);
}

TEST_CASE("Overlapping I/O range is refused and reserves nothing", "[resource]") {
  common::DoubleList<CmPartialResourceDescriptor> reserved;
  CmPartialResourceDescriptor held = MakeCm(ResourceType::Port, 0x1F0, 8);// [0x1F0,0x1F7]
  reserved.push_back(&held);

  IoResourceList alt;
  IoResourceDescriptor overlapping = MakeDesc(ResourceType::Port, 0x1F4, 4);// [0x1F4,0x1F7]
  alt.descriptors.push_back(&overlapping);

  CmPartialResourceDescriptor pool[4];
  size_t used = 0;
  auto alloc = [&]() -> CmPartialResourceDescriptor * { return used < 4 ? &pool[used++] : nullptr; };
  CmResourceList grant;
  REQUIRE_FALSE(TryReserveAlternative(alt, reserved, grant, alloc));
  REQUIRE(grant.descriptors.empty());
  REQUIRE(used == 0);
}

TEST_CASE("Shared IRQ coexists but exclusive conflicts", "[resource]") {
  SECTION("Two Shared interrupts on the same vector do not conflict") {
    common::DoubleList<CmPartialResourceDescriptor> reserved;
    CmPartialResourceDescriptor held = MakeCm(ResourceType::Interrupt, 14, 1, ResourceShare::Shared);
    reserved.push_back(&held);

    IoResourceDescriptor shared = MakeDesc(ResourceType::Interrupt, 14, 1, ResourceShare::Shared);
    REQUIRE_FALSE(ConflictsWithReserved(shared, reserved));
  }

  SECTION("An exclusive request on a shared-held vector conflicts") {
    common::DoubleList<CmPartialResourceDescriptor> reserved;
    CmPartialResourceDescriptor held = MakeCm(ResourceType::Interrupt, 14, 1, ResourceShare::Shared);
    reserved.push_back(&held);

    IoResourceDescriptor exclusive = MakeDesc(ResourceType::Interrupt, 14, 1, ResourceShare::DeviceExclusive);
    REQUIRE(ConflictsWithReserved(exclusive, reserved));
  }

  SECTION("A shared request on an exclusively-held vector conflicts") {
    common::DoubleList<CmPartialResourceDescriptor> reserved;
    CmPartialResourceDescriptor held = MakeCm(ResourceType::Interrupt, 14, 1, ResourceShare::DeviceExclusive);
    reserved.push_back(&held);

    IoResourceDescriptor shared = MakeDesc(ResourceType::Interrupt, 14, 1, ResourceShare::Shared);
    REQUIRE(ConflictsWithReserved(shared, reserved));
  }

  SECTION("Different resource types never conflict") {
    common::DoubleList<CmPartialResourceDescriptor> reserved;
    CmPartialResourceDescriptor held = MakeCm(ResourceType::Port, 14, 1);
    reserved.push_back(&held);

    IoResourceDescriptor irq = MakeDesc(ResourceType::Interrupt, 14, 1);
    REQUIRE_FALSE(ConflictsWithReserved(irq, reserved));
  }
}

TEST_CASE("Empty alternative reserves an empty grant", "[resource]") {
  IoResourceList alt;// no descriptors
  common::DoubleList<CmPartialResourceDescriptor> reserved;
  auto alloc = []() -> CmPartialResourceDescriptor * { return nullptr; };
  CmResourceList grant;
  REQUIRE(TryReserveAlternative(alt, reserved, grant, alloc));
  REQUIRE(grant.descriptors.empty());
}
