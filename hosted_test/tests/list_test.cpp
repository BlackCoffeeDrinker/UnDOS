#include <list.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("list default is empty", "[list]") {
    kstd::list<int> l;
    REQUIRE(l.empty());
    REQUIRE(l.size() == 0);
}

TEST_CASE("list push_back and push_front preserve order", "[list]") {
    kstd::list<int> l;
    l.push_back(1);
    l.push_back(2);
    l.push_front(0);
    REQUIRE(l.size() == 3);

    auto it = l.begin();
    REQUIRE(*it == 0);
    ++it;
    REQUIRE(*it == 1);
    ++it;
    REQUIRE(*it == 2);
    ++it;
    REQUIRE(it == l.end());
}

TEST_CASE("list initializer_list", "[list]") {
    kstd::list<int> l = {1, 2, 3, 4};
    REQUIRE(l.size() == 4);
    REQUIRE(l.front() == 1);
    REQUIRE(l.back() == 4);
}

TEST_CASE("list copy and move", "[list]") {
    kstd::list<int> l = {1, 2, 3};
    kstd::list<int> copy(l);
    REQUIRE(copy.size() == 3);
    REQUIRE(copy.front() == 1);

    kstd::list<int> moved(kstd::move(copy));
    REQUIRE(moved.size() == 3);
    REQUIRE(moved.front() == 1);
}

TEST_CASE("list pop_back and pop_front", "[list]") {
    kstd::list<int> l = {1, 2, 3};
    l.pop_back();
    REQUIRE(l.size() == 2);
    REQUIRE(l.back() == 2);
    l.pop_front();
    REQUIRE(l.size() == 1);
    REQUIRE(l.front() == 2);
}

TEST_CASE("list clear", "[list]") {
    kstd::list<int> l = {1, 2, 3};
    l.clear();
    REQUIRE(l.empty());
}

TEST_CASE("list insert and erase", "[list]") {
    kstd::list<int> l = {1, 2, 4};
    auto it = l.begin();
    ++it;
    ++it;
    l.insert(it, 3);

    int expected[] = {1, 2, 3, 4};
    size_t i = 0;
    for (auto v : l) {
        REQUIRE(v == expected[i]);
        ++i;
    }
    REQUIRE(l.size() == 4);

    it = l.begin();
    ++it;
    it = l.erase(it);
    REQUIRE(*it == 3);
    REQUIRE(l.size() == 3);
}

TEST_CASE("list reverse iteration", "[list]") {
    kstd::list<int> l = {1, 2, 3};
    int expected[] = {3, 2, 1};
    size_t i = 0;
    for (auto it = l.rbegin(); it != l.rend(); ++it) {
        REQUIRE(*it == expected[i]);
        ++i;
    }
}

TEST_CASE("list with non-trivial type tracks destructor calls", "[list]") {
    static int live_count = 0;
    struct Counter {
        Counter() { live_count++; }
        Counter(const Counter &) { live_count++; }
        Counter(Counter &&) noexcept { live_count++; }
        ~Counter() { live_count--; }
    };

    {
        kstd::list<Counter> l;
        for (int i = 0; i < 8; i++) {
            l.emplace_back();
        }
        REQUIRE(live_count == 8);
        l.clear();
        REQUIRE(live_count == 0);
    }
    REQUIRE(live_count == 0);
}
