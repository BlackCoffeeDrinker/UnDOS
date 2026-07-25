#include <vector.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("vector default is empty", "[vector]") {
    kstd::vector<int> v;
    REQUIRE(v.empty());
    REQUIRE(v.size() == 0);
    REQUIRE(v.capacity() == 0);
}

TEST_CASE("vector push_back grows and preserves order", "[vector]") {
    kstd::vector<int> v;
    for (size_t i = 0; i < 10; i++) {
        v.push_back(static_cast<int>(i));
    }
    REQUIRE(v.size() == 10);
    for (size_t i = 0; i < 10; i++) {
        REQUIRE(v[i] == static_cast<int>(i));
    }
}

TEST_CASE("vector initializer_list", "[vector]") {
    kstd::vector<int> v = {1, 2, 3, 4};
    REQUIRE(v.size() == 4);
    REQUIRE(v[0] == 1);
    REQUIRE(v[3] == 4);
}

TEST_CASE("vector copy and move", "[vector]") {
    kstd::vector<int> v = {1, 2, 3};
    kstd::vector<int> copy(v);
    REQUIRE(copy.size() == 3);
    REQUIRE(copy[0] == 1);

    kstd::vector<int> moved(kstd::move(copy));
    REQUIRE(moved.size() == 3);
    REQUIRE(moved[0] == 1);
}

TEST_CASE("vector pop_back and clear", "[vector]") {
    kstd::vector<int> v = {1, 2, 3};
    v.pop_back();
    REQUIRE(v.size() == 2);
    v.clear();
    REQUIRE(v.empty());
}

TEST_CASE("vector resize", "[vector]") {
    kstd::vector<int> v;
    v.resize(5, 7);
    REQUIRE(v.size() == 5);
    for (size_t i = 0; i < 5; i++) {
        REQUIRE(v[i] == 7);
    }
    v.resize(2);
    REQUIRE(v.size() == 2);
}

TEST_CASE("vector with non-trivial type tracks destructor calls", "[vector]") {
    static int live_count = 0;
    struct Counter {
        Counter() { live_count++; }
        Counter(const Counter &) { live_count++; }
        Counter(Counter &&) noexcept { live_count++; }
        ~Counter() { live_count--; }
    };

    {
        kstd::vector<Counter> v;
        for (int i = 0; i < 8; i++) {
            v.emplace_back();
        }
        REQUIRE(live_count == 8);
        v.clear();
        REQUIRE(live_count == 0);
    }
    REQUIRE(live_count == 0);
}
