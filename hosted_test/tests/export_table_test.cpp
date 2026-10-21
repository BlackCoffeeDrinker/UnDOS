#include <catch2/catch_test_macros.hpp>

#include <kernel/module/export_table.hpp>

TEST_CASE("ExportTable keeps distinct long names apart", "[export-table]") {
  const char *first = "_ZN6kernel8KObjectTINS_17KVttySourceObjectELm1EXtlNS_16StaticIdentifierINS_13ObjectTypeTagEEEED1Ev";
  const char *second = "_ZN6kernel8KObjectTINS_17KVttySourceObjectELm1EXtlNS_16StaticIdentifierINS_13ObjectTypeTagEEEED2Ev";
  kernel::ExportTable table;
  REQUIRE_FALSE(table.Add(second, kernel::VirtualAddress{0x5678}));
  REQUIRE_FALSE(table.Add(first, kernel::VirtualAddress{0x1234}));
  REQUIRE_FALSE(table.Add("zeta", kernel::VirtualAddress{3}));
  REQUIRE_FALSE(table.Add("alpha", kernel::VirtualAddress{4}));
  REQUIRE_FALSE(table.Seal());
  REQUIRE(table.sealed());
  REQUIRE(table.Find(first).value().value == 0x1234);
  REQUIRE(table.Find(second).value().value == 0x5678);
  REQUIRE(table.Find("alpha").value().value == 4);
  REQUIRE(table.Find("zeta").value().value == 3);
  REQUIRE_FALSE(table.Find("missing").has_value());
  REQUIRE_FALSE(table.Find("alph").has_value());
}

TEST_CASE("ExportTable rejects duplicates when sealing", "[export-table]") {
  kernel::ExportTable table;
  REQUIRE_FALSE(table.Add("same", kernel::VirtualAddress{1}));
  REQUIRE_FALSE(table.Add("same", kernel::VirtualAddress{2}));
  REQUIRE(table.Seal() == kernel::KernelError::AlreadyExists);
  REQUIRE_FALSE(table.sealed());
}
