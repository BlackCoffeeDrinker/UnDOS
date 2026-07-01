
#include "static_string.hpp"


#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

TEST_CASE("Static_String == ConstPTR") {
  kstd::static_string<32> a;
  a = "Hello";
  const char* b = "Hello";
  REQUIRE(a == b);
}
