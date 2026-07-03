#include <catch2/catch_test_macros.hpp>
#include <kernel/StaticIdentifier.hpp>
#include <string_view.hpp>

using namespace kernel;

struct TagA {
    static constexpr uint64_t seed = 0x1234567812345678ull;
};

struct TagB {
    static constexpr uint64_t seed = 0x8765432187654321ull;
};

struct TagNoSeed {};

using IdentA = StaticIdentifier<TagA>;
using IdentB = StaticIdentifier<TagB>;
using IdentNoSeed = StaticIdentifier<TagNoSeed>;

TEST_CASE("StaticIdentifier basic functionality", "[StaticIdentifier]") {
    SECTION("Hash consistency") {
        auto a1 = IdentA::from_literal("test");
        auto a2 = IdentA::from_string("test");
        auto a3 = IdentA::from_string(kstd::string_view{"test"});
        REQUIRE(a1 == a2);
        REQUIRE(a1 == a3);
        REQUIRE(a1.value != 0);
    }

    SECTION("Different strings produce different hashes") {
        auto a1 = IdentA::from_literal("test1");
        auto a2 = IdentA::from_literal("test2");
        REQUIRE(a1 != a2);
    }

    SECTION("Different tags produce different hashes for same string") {
        auto a = IdentA::from_literal("test");
        auto b = IdentB::from_literal("test");
        auto n = IdentNoSeed::from_literal("test");
        
        REQUIRE(a.value != b.value);
        REQUIRE(a.value != n.value);
        REQUIRE(b.value != n.value);
    }

    SECTION("Type safety") {
        IdentA a = IdentA::from_literal("test");
        IdentA a2 = a;
        REQUIRE(a == a2);
        // IdentB b = a; // Static check: IdentA and IdentB are different types
    }
    
    SECTION("Comparison operators") {
        auto a1 = IdentA::from_literal("abc");
        auto a2 = IdentA::from_literal("def");
        
        if (a1.value < a2.value) {
            REQUIRE(a1 < a2);
            REQUIRE(a1 <= a2);
            REQUIRE(a2 > a1);
            REQUIRE(a2 >= a1);
        } else {
            REQUIRE(a2 < a1);
            REQUIRE(a2 <= a1);
            REQUIRE(a1 > a2);
            REQUIRE(a1 >= a2);
        }
    }
}

TEST_CASE("fnv1a64 hasher", "[fnv1a64]") {
    SECTION("Known hashes") {
        // Standard FNV-1a 64-bit for empty string with default offset basis
        uint64_t empty = fnv1a64::hash("", 0);
        REQUIRE(empty == fnv1a64::offset_basis);

        // "a"
        uint64_t a = fnv1a64::hash("a", 1);
        uint64_t expected_a = (fnv1a64::offset_basis ^ static_cast<uint8_t>('a')) * fnv1a64::prime;
        REQUIRE(a == expected_a);
    }
}
