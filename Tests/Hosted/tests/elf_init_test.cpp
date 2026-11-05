#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

#include "dispatch.hpp"
#include "elf_init.hpp"

namespace {
using kernel::elf::InitTables;
using kernel::elf::kInitTableCtors;
using kernel::elf::kInitTableInitArray;
using kernel::elf::RunInitTables;
using Fn = void (*)();

std::vector<int> g_calls;
void First() { g_calls.push_back(1); }
void Second() { g_calls.push_back(2); }
void Third() { g_calls.push_back(3); }

// Image bounds that cover the whole address space, so host functions and
// stack tables count as "inside" for ordering tests.
InitTables WholeSpace() {
  InitTables t;
  t.image_base = 0;
  t.image_size = SIZE_MAX;
  return t;
}

// The kernel's dispatch core must be constant-initialisable; this fails to
// compile if DpcCore regains a non-constexpr constructor.
constinit kernel::dispatch::DpcCore g_constinitCore;
}// namespace

TEST_CASE("RunInitTables runs .ctors then .init_array in order, skipping sentinels", "[elf][ctors]") {
  g_calls.clear();
  Fn ctors[] = {First, reinterpret_cast<Fn>(static_cast<uintptr_t>(-1)), Second};
  Fn initArray[] = {nullptr, Third};
  auto t = WholeSpace();
  t.tables[kInitTableCtors] = {reinterpret_cast<uintptr_t>(ctors), 3};
  t.tables[kInitTableInitArray] = {reinterpret_cast<uintptr_t>(initArray), 2};

  REQUIRE_FALSE(RunInitTables(t));
  CHECK(g_calls == std::vector<int>{1, 2, 3});
}

TEST_CASE("RunInitTables is a no-op for an image without tables", "[elf][ctors]") {
  g_calls.clear();
  InitTables t;
  REQUIRE_FALSE(RunInitTables(t));
  CHECK(g_calls.empty());
}

TEST_CASE("RunInitTables rejects an entry outside the image and runs nothing", "[elf][ctors]") {
  g_calls.clear();
  // The "image" is this buffer; the table lives inside it but the second
  // entry points at host code outside it.
  alignas(Fn) Fn image[4] = {First, Second, nullptr, nullptr};
  InitTables t;
  t.image_base = reinterpret_cast<uintptr_t>(image);
  t.image_size = sizeof(image);
  t.tables[kInitTableCtors] = {reinterpret_cast<uintptr_t>(image), 2};

  CHECK(RunInitTables(t) == kernel::KernelError::InvalidExecutable);
  CHECK(g_calls.empty());
}

TEST_CASE("RunInitTables rejects a table that extends past the image", "[elf][ctors]") {
  g_calls.clear();
  alignas(Fn) Fn image[2] = {nullptr, nullptr};
  InitTables t;
  t.image_base = reinterpret_cast<uintptr_t>(image);
  t.image_size = sizeof(image);
  t.tables[kInitTableInitArray] = {reinterpret_cast<uintptr_t>(image), 3};

  CHECK(RunInitTables(t) == kernel::KernelError::InvalidExecutable);
  CHECK(g_calls.empty());
}

TEST_CASE("RunInitTables never runs user-mode images", "[elf][ctors]") {
  g_calls.clear();
  Fn ctors[] = {First};
  auto t = WholeSpace();
  t.user_mode = true;
  t.tables[kInitTableCtors] = {reinterpret_cast<uintptr_t>(ctors), 1};

  CHECK(RunInitTables(t) == kernel::KernelError::InvalidArgument);
  CHECK(g_calls.empty());
}

TEST_CASE("constinit DpcCore starts with the full reservation budget", "[elf][ctors][dispatch]") {
  CHECK(g_constinitCore.ReservedCount() == 0);
  CHECK(g_constinitCore.QueueEmpty());
}
