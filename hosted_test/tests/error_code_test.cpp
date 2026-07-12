
#include <catch2/catch_test_macros.hpp>
#include <system_error.hpp>

TEST_CASE("error_code default", "[error_code]") {
    kstd::error_code ec;
    REQUIRE(ec.value() == 0);
    REQUIRE(!ec);
    REQUIRE(ec.category() == kstd::system_category());
}

TEST_CASE("error_condition default", "[error_code]") {
    kstd::error_condition cond;
    REQUIRE(cond.value() == 0);
    REQUIRE(!cond);
    REQUIRE(cond.category() == kstd::generic_category());
}

TEST_CASE("error_code clear", "[error_code]") {
    kstd::error_code ec(42, kstd::generic_category());
    REQUIRE(ec);
    ec.clear();
    REQUIRE(ec.value() == 0);
    REQUIRE(ec.category() == kstd::system_category());
}

TEST_CASE("error_code comparisons", "[error_code]") {
    kstd::error_code ec1(7, kstd::generic_category());
    kstd::error_code ec2(7, kstd::generic_category());
    kstd::error_condition cond(7, kstd::generic_category());

    REQUIRE(ec1 == ec2);
    REQUIRE(ec1 == cond);
    REQUIRE(cond == ec1);
}
