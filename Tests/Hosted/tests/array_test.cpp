#include <array.hpp>
#include <strfmt.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("array size 0", "[array]") {
    kstd::array<int, 0> a;
    REQUIRE(a.size() == 0);
    REQUIRE(a.begin() == a.end());
    // a.data() could be nullptr or something else
}

TEST_CASE("array get", "[array]") {
    kstd::array<int, 3> a = {1, 2, 3};
    REQUIRE(kstd::get<0>(a) == 1);
    REQUIRE(kstd::get<1>(a) == 2);
    REQUIRE(kstd::get<2>(a) == 3);
}

TEST_CASE("format with no args", "[strfmt]") {
    char buf[32];
    kstd::nomm_string s(buf);
    kstd::format(s, "hello");
    REQUIRE(kstd::string_view(s) == "hello");
}
