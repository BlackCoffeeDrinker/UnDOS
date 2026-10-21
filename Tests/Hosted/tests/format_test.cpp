
#include "strfmt.hpp"
#include "static_string.hpp"
#include "limits.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>

TEST_CASE("kstd::format with static_string", "[strfmt]") {
    kstd::static_string<64> s;
    
    SECTION("Basic static_string formatting") {
        kstd::format(s, "Hello {}!", "World");
        REQUIRE(s == "Hello World!");
    }

    SECTION("Consecutive static_string formats") {
        kstd::format(s, "First");
        REQUIRE(s == "First");
        kstd::format(s, "Second");
        REQUIRE(s == "Second");
    }

    SECTION("Integer formatting") {
        kstd::format(s, "Count: {}", 42);
        REQUIRE(s == "Count: 42");
    }

    SECTION("Hex formatting") {
        kstd::format(s, "Hex: {x}", 0xABC);
        REQUIRE(s == "Hex: abc");
    }

    SECTION("Multiple arguments") {
        kstd::format(s, "{} + {} = {}", 1, 2, 3);
        REQUIRE(s == "1 + 2 = 3");
    }
    
    SECTION("Truncation") {
        kstd::static_string<5> small;
        kstd::format(small, "Hello World");
        REQUIRE(small == "Hell");
    }

    SECTION("Missing arguments") {
        kstd::format(s, "Hello {}!", "World"); // Reset
        kstd::format(s, "Missing {} arg", 1); // Normal
        REQUIRE(s == "Missing 1 arg");
        
        kstd::format(s, "Missing {} arg"); // Missing
        REQUIRE(s == "Missing  arg"); // The {} is skipped
    }
}

TEST_CASE("kstd::format with nomm_string", "[strfmt]") {
    char buffer[64] = {0};
    kstd::nomm_string s(buffer);
    
    SECTION("Basic nomm_string formatting") {
        kstd::format(s, "Hello {}!", "World");
        REQUIRE(s == "Hello World!");
    }

    SECTION("nomm_string truncation") {
        char tiny_buffer[6] = {0};
        kstd::nomm_string tiny(tiny_buffer);
        kstd::format(tiny, "Hello World");
        REQUIRE(tiny == "Hello");
    }
}

TEST_CASE("kstd::format preserves argument and parser behavior", "[strfmt]") {
  kstd::static_string<128> s;

  SECTION("Unsigned integer limits and zero") {
    kstd::format(s, "{} {x} {}", 0u, 0xffffffffffffffffull, 18446744073709551615ull);
    REQUIRE(s == "0 ffffffffffffffff 18446744073709551615");
  }

  SECTION("Signed minimum values do not overflow") {
    kstd::format(s, "{}", kstd::numeric_limits<int>::min());
    REQUIRE(s == "-2147483648");
    kstd::format(s, "{}", kstd::numeric_limits<long>::min());
    if constexpr (sizeof(long) == 8) {
      REQUIRE(s == "-9223372036854775808");
    } else {
      REQUIRE(s == "-2147483648");
    }
  }

  SECTION("Negative values retain decimal output with hex options") {
    kstd::format(s, "{x} {x}", -42, -42L);
    REQUIRE(s == "-42 -42");
  }

  SECTION("Strings, characters, booleans, enums and pointers") {
    enum class Value : unsigned { test = 42 };
    const void *pointer = nullptr;
    kstd::format(s, "{} {} {} {} {} {}", kstd::string_view("view"), '!', true, false, Value::test, pointer);
    REQUIRE(s == "view ! True False 42 0x0");
  }

  SECTION("Missing and surplus arguments") {
    kstd::format(s, "{}|{x}|{}", 7u);
    REQUIRE(s == "7||");
    kstd::format(s, "plain", 7u, 8u);
    REQUIRE(s == "plain");
  }

  SECTION("An unclosed placeholder stops formatting") {
    kstd::format(s, "prefix {", 7u);
    REQUIRE(s == "prefix ");
  }

  SECTION("Callback sinks still accept zero and multiple arguments") {
    auto sink = [&s](char c) { s.append(c); };
    kstd::format_dst(sink, "plain");
    kstd::format_dst(sink, " {} {x}", 7, 42u);
    REQUIRE(s == "plain 7 2a");
  }
}
