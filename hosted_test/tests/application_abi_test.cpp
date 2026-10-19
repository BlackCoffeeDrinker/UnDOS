#include <catch2/catch_test_macros.hpp>
#include <undos/abi_note.hpp>
#include "../../userland/crt/include/undoscrt/lifecycle.hpp"

TEST_CASE("Application startup rejects unknown versions and reserved fields", "[application-abi]") {
  UnDOSStartupInfoV1 info{};
  info.size = sizeof(info);
  info.version = 1;
  info.argc = 1;
  info.argv = 0x10000;
  info.envp = 0x10008;
  REQUIRE(undos::abi::valid_startup(info));
  info.version = 2;
  REQUIRE_FALSE(undos::abi::valid_startup(info));
  info.version = 1;
  info.size -= 4;
  REQUIRE_FALSE(undos::abi::valid_startup(info));
  info.size = sizeof(info);
  info.reserved[3] = 1;
  REQUIRE_FALSE(undos::abi::valid_startup(info));
}

TEST_CASE("UnDOS ABI notes reject malformed incompatible and duplicate descriptors", "[application-abi]") {
  uint8_t note[] = {6, 0, 0, 0, 8, 0, 0, 0, 1, 0, 0, 0,
                    'U', 'n', 'D', 'O', 'S', 0, 0, 0,
                    1, 0, 0, 0, 0, 0, 0, 0};
  uint32_t matches = 0;
  REQUIRE(undos::abi::scan_notes(note, sizeof(note), matches));
  REQUIRE(matches == 1);
  REQUIRE_FALSE(undos::abi::scan_notes(note, sizeof(note), matches));
  for (uint32_t size = 1; size < sizeof(note); ++size) {
    matches = 0;
    REQUIRE_FALSE(undos::abi::scan_notes(note, size, matches));
  }
  matches = 0;
  note[20] = 2;
  REQUIRE_FALSE(undos::abi::scan_notes(note, sizeof(note), matches));
  note[20] = 1;
  note[24] = 1;
  REQUIRE_FALSE(undos::abi::scan_notes(note, sizeof(note), matches));
}

namespace {
int calls[8]{};
int count;
void record(void *value) { calls[count++] = *static_cast<int *>(value); }
}

TEST_CASE("Exit callbacks are LIFO DSO-selective and execute at most once", "[application-abi]") {
  undos::crt::ExitCallbacks callbacks;
  count = 0;
  int first = 1, second = 2, third = 3;
  int dso_a{}, dso_b{};
  REQUIRE(callbacks.add(record, &first, &dso_a) == 0);
  REQUIRE(callbacks.add(record, &second, &dso_b) == 0);
  REQUIRE(callbacks.add(record, &third, &dso_a) == 0);
  callbacks.finalize(&dso_a);
  REQUIRE(count == 2);
  REQUIRE(calls[0] == 3);
  REQUIRE(calls[1] == 1);
  callbacks.finalize();
  REQUIRE(count == 3);
  REQUIRE(calls[2] == 2);
  callbacks.finalize();
  REQUIRE(count == 3);
}

TEST_CASE("Exit callback registration reports exhaustion and reuses drained storage", "[application-abi]") {
  undos::crt::ExitCallbacks callbacks;
  REQUIRE(callbacks.add(static_cast<void (*)()>(nullptr)) == -1);
  for (unsigned i = 0; i < 128; ++i) REQUIRE(callbacks.add(+[] {}) == 0);
  REQUIRE(callbacks.add(+[] {}) == -1);
  callbacks.finalize();
  REQUIRE(callbacks.add(+[] {}) == 0);
}
