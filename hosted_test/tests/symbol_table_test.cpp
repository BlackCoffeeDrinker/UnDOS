#include <catch2/catch_test_macros.hpp>

#include "module/symbol_table.hpp"
#include "ob_test_support.hpp"

TEST_CASE("Boot symbol handoff preserves distinct long names", "[symbol-table]") {
  EnsureObjectManagerInitialized();
  const char *first = "_ZN6kernel8KObjectTINS_17KVttySourceObjectELm1EXtlNS_16StaticIdentifierINS_13ObjectTypeTagEEEED1Ev";
  const char *second = "_ZN6kernel8KObjectTINS_17KVttySourceObjectELm1EXtlNS_16StaticIdentifierINS_13ObjectTypeTagEEEED2Ev";
  kernel::BootSymbolsT firstBoot{}, secondBoot{};
  firstBoot.name = first;
  secondBoot.name = second;
  REQUIRE(kstd::string_view(firstBoot.name) == first);
  REQUIRE(kstd::string_view(secondBoot.name) == second);

  kernel::driver::SymbolTable table;
  REQUIRE_FALSE(table.Register(firstBoot.name, 0x1234));
  REQUIRE_FALSE(table.Register(secondBoot.name, 0x5678));
  REQUIRE(table.Resolve(first).value() == 0x1234);
  REQUIRE(table.Resolve(second).value() == 0x5678);
  REQUIRE(table.Register(first, 0x9999) == kernel::KernelError::AlreadyExists);
  REQUIRE(table.Resolve(first).value() == 0x1234);
}

TEST_CASE("Symbol table rejects oversized names without truncating", "[symbol-table]") {
  EnsureObjectManagerInitialized();
  kernel::driver::SymbolTable table;
  char name[257];
  for (size_t i = 0; i < sizeof(name) - 1; ++i) name[i] = 'x';
  name[sizeof(name) - 1] = '\0';
  REQUIRE(table.Register(name, 1) == kernel::KernelError::NameTooLong);
  name[255] = '\0';
  REQUIRE_FALSE(table.Register(name, 2));
  REQUIRE(table.Resolve(name).value() == 2);
}
