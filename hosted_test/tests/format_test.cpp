
#include "strfmt.hpp"
#include "static_string.hpp"
#include <catch2/catch_test_macros.hpp>
#include <string>

TEST_CASE("kstd::format with static_string") {
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

TEST_CASE("kstd::format with nomm_string") {
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
