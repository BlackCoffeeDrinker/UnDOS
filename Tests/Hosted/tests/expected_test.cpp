
#include <catch2/catch_test_macros.hpp>
#include <expected.hpp>
#include <string_view.hpp>

TEST_CASE("expected basic", "[expected]") {
    kstd::expected<int, const char*> e = 42;
    REQUIRE(e.has_value());
    REQUIRE(*e == 42);
    REQUIRE(e.value() == 42);
}

TEST_CASE("expected error", "[expected]") {
    kstd::expected<int, const char*> e = kstd::unexpected("error");
    REQUIRE(!e.has_value());
    REQUIRE(kstd::string_view(e.error()) == "error");
}

TEST_CASE("expected void", "[expected]") {
    kstd::expected<void, int> e;
    REQUIRE(e.has_value());
    e = kstd::unexpected(42);
    REQUIRE(!e.has_value());
    REQUIRE(e.error() == 42);
}

TEST_CASE("expected unexpect constructor", "[expected]") {
    kstd::expected<int, int> e(kstd::unexpect, 42);
    REQUIRE(!e.has_value());
    REQUIRE(e.error() == 42);
}

TEST_CASE("expected in_place constructor", "[expected]") {
    kstd::expected<int, int> e(kstd::in_place, 42);
    REQUIRE(e.has_value());
    REQUIRE(*e == 42);
}

TEST_CASE("expected emplace", "[expected]") {
    kstd::expected<int, int> e = kstd::unexpected(42);
    REQUIRE(!e.has_value());
    e.emplace(123);
    REQUIRE(e.has_value());
    REQUIRE(*e == 123);
}

TEST_CASE("expected comparison", "[expected]") {
    kstd::expected<int, int> e1 = 42;
    kstd::expected<int, int> e2 = 42;
    kstd::expected<int, int> e3 = 43;
    kstd::expected<int, int> e4 = kstd::unexpected(42);

    REQUIRE(e1 == e2);
    REQUIRE(e1 != e3);
    REQUIRE(e1 != e4);
    REQUIRE(e1 == 42);
    REQUIRE(e4 == kstd::unexpected(42));
}
