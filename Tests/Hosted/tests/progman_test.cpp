#include <catch2/catch_test_macros.hpp>
#include "../../../Apps/Progman/model.hpp"

TEST_CASE("Program Manager configuration and stable identities", "[progman]") {
  progman::Model model;
  char config[] = "version=1\r\ngroup|4|Main\nitem|9|4|Demo|/System/demo.elf|\"two words\"|1\n";
  REQUIRE(progman::parse(model, config) == nullptr);
  REQUIRE_FALSE(model.modified);
  REQUIRE(model.count(4) == 1);
  REQUIRE(model.items[0].id == 9);
  REQUIRE(model.add_group("Accessories") == 1);
  REQUIRE(model.groups[1].id == 10);
  REQUIRE(model.remove_group(4));
  REQUIRE(model.count(4) == 0);
  REQUIRE(model.item_index(9) == -1);
}

TEST_CASE("Program Manager rejects malformed configuration", "[progman]") {
  progman::Model model;
  char version[] = "version=2\n";
  char duplicate[] = "version=1\ngroup|1|A\ngroup|1|B\n";
  char parent[] = "version=1\nitem|4|9|Demo|/System/demo.elf||0\n";
  char overflow[] = "version=1\ngroup|4294967296|Bad\n";
  char extra[] = "version=1\ngroup|1|Main|extra\n";
  REQUIRE(progman::parse(model, version) != nullptr);
  REQUIRE(progman::parse(model, duplicate) != nullptr);
  REQUIRE(progman::parse(model, parent) != nullptr);
  REQUIRE(progman::parse(model, overflow) != nullptr);
  REQUIRE(progman::parse(model, extra) != nullptr);
}

TEST_CASE("Program Manager model changes are bounded and validated", "[progman]") {
  progman::Model model;
  REQUIRE(model.add_group("") == -1);
  REQUIRE(model.add_group("bad|name") == -1);
  REQUIRE(model.add_group("Main") == 0);
  REQUIRE(model.add_item(1, "Demo", "relative.elf", "", 0) == -1);
  REQUIRE(model.add_item(8, "Demo", "/demo.elf", "", 0) == -1);
  for (unsigned i = 0; i < progman::MaxItems; ++i)
    REQUIRE(model.add_item(1, "Demo", "/demo.elf", "", 0) >= 0);
  REQUIRE(model.add_item(1, "Full", "/demo.elf", "", 0) == -1);
}

TEST_CASE("Program Manager paging keeps selected icons reachable", "[progman]") {
  const auto grid = progman::Grid::for_size(240, 180);
  REQUIRE(grid.columns == 2);
  REQUIRE(grid.rows == 2);
  REQUIRE(grid.page_start(5) == 4);
  REQUIRE(progman::Grid::for_size(224, 144).page_size == 4);
  const auto tiny = progman::Grid::for_size(1, 1);
  REQUIRE(tiny.page_size == 1);
}

TEST_CASE("Program Manager identity exhaustion cannot wrap into an existing ID", "[progman]") {
  progman::Model model;
  char config[] = "version=1\ngroup|6|Main\ngroup|4294967294|Last\n"
                  "item|5|6|Demo|/System/demo.elf||0\n";
  REQUIRE(progman::parse(model, config) == nullptr);
  REQUIRE(model.add_group("Final") == 2);
  REQUIRE(model.groups[2].id == UINT32_MAX);
  REQUIRE(model.next_id() == 0);
  REQUIRE(model.add_group("New") == -1);
  REQUIRE(model.add_item(6, "New", "/System/demo.elf", "", 0) == -1);
  REQUIRE(model.count(6) == 1);
  REQUIRE(model.group_index(6) == 0);
}
