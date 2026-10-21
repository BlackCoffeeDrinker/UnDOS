#include <map.hpp>
#include <catch2/catch_test_macros.hpp>

TEST_CASE("map default is empty", "[map]") {
    kstd::map<int, int> m;
    REQUIRE(m.empty());
    REQUIRE(m.size() == 0);
    REQUIRE(m.begin() == m.end());
}

TEST_CASE("map insert and find", "[map]") {
    kstd::map<int, int> m;
    auto [it1, inserted1] = m.insert({1, 100});
    REQUIRE(inserted1);
    REQUIRE(it1->first == 1);
    REQUIRE(it1->second == 100);

    auto [it2, inserted2] = m.insert({1, 200});
    REQUIRE_FALSE(inserted2);
    REQUIRE(it2->second == 100);

    REQUIRE(m.size() == 1);
    REQUIRE(m.contains(1));
    REQUIRE_FALSE(m.contains(2));

    auto found = m.find(1);
    REQUIRE(found != m.end());
    REQUIRE(found->second == 100);
    REQUIRE(m.find(42) == m.end());
}

TEST_CASE("map operator[] inserts default and updates", "[map]") {
    kstd::map<int, int> m;
    m[5] = 50;
    REQUIRE(m.size() == 1);
    REQUIRE(m[5] == 50);

    m[5] = 51;
    REQUIRE(m.size() == 1);
    REQUIRE(m[5] == 51);

    REQUIRE(m[6] == 0);
    REQUIRE(m.size() == 2);
}

TEST_CASE("map iteration is sorted by key", "[map]") {
    kstd::map<int, int> m;
    int values[] = {5, 3, 8, 1, 4, 7, 2, 6, 0, 9};
    for (int v : values) {
        m[v] = v * 10;
    }
    REQUIRE(m.size() == 10);

    int expected = 0;
    for (const auto &kv : m) {
        REQUIRE(kv.first == expected);
        REQUIRE(kv.second == expected * 10);
        ++expected;
    }
    REQUIRE(expected == 10);
}

TEST_CASE("map reverse iteration is sorted descending", "[map]") {
    kstd::map<int, int> m;
    for (int v = 0; v < 10; ++v) m[v] = v;

    int expected = 9;
    for (auto it = m.rbegin(); it != m.rend(); ++it) {
        REQUIRE(it->first == expected);
        --expected;
    }
    REQUIRE(expected == -1);
}

TEST_CASE("map erase by key and iterator", "[map]") {
    kstd::map<int, int> m;
    for (int v = 0; v < 10; ++v) m[v] = v;

    REQUIRE(m.erase(5) == 1);
    REQUIRE(m.erase(5) == 0);
    REQUIRE(m.size() == 9);
    REQUIRE_FALSE(m.contains(5));

    auto it = m.find(3);
    auto next = m.erase(it);
    REQUIRE(next->first == 4);
    REQUIRE(m.size() == 8);

    int expected[] = {0, 1, 2, 4, 6, 7, 8, 9};
    size_t i = 0;
    for (const auto &kv : m) {
        REQUIRE(kv.first == expected[i]);
        ++i;
    }
    REQUIRE(i == 8);
}

TEST_CASE("map erase all elements shrinks to empty", "[map]") {
    kstd::map<int, int> m;
    for (int v = 0; v < 50; ++v) m[v] = v;

    for (int v = 0; v < 50; ++v) {
        REQUIRE(m.erase(v) == 1);
    }
    REQUIRE(m.empty());
    REQUIRE(m.begin() == m.end());
}

TEST_CASE("map stress insert/erase keeps sorted order and correct size", "[map]") {
    kstd::map<int, int> m;
    constexpr int N = 200;

    // Insert in a scrambled but deterministic order.
    for (int i = 0; i < N; ++i) {
        int key = (i * 37) % N;
        m[key] = key;
    }
    REQUIRE(m.size() == static_cast<size_t>(N));

    int prev = -1;
    size_t count = 0;
    for (const auto &kv : m) {
        REQUIRE(kv.first > prev);
        prev = kv.first;
        ++count;
    }
    REQUIRE(count == static_cast<size_t>(N));

    // Erase every other key.
    for (int i = 0; i < N; i += 2) {
        REQUIRE(m.erase(i) == 1);
    }
    REQUIRE(m.size() == static_cast<size_t>(N / 2));

    prev = -1;
    count = 0;
    for (const auto &kv : m) {
        REQUIRE(kv.first % 2 == 1);
        REQUIRE(kv.first > prev);
        prev = kv.first;
        ++count;
    }
    REQUIRE(count == static_cast<size_t>(N / 2));
}

TEST_CASE("map copy and move", "[map]") {
    kstd::map<int, int> m;
    m[1] = 10;
    m[2] = 20;

    kstd::map<int, int> copy(m);
    REQUIRE(copy.size() == 2);
    REQUIRE(copy[1] == 10);
    REQUIRE(m.size() == 2);

    kstd::map<int, int> moved(kstd::move(copy));
    REQUIRE(moved.size() == 2);
    REQUIRE(moved[2] == 20);
}

TEST_CASE("map clear", "[map]") {
    kstd::map<int, int> m;
    for (int v = 0; v < 20; ++v) m[v] = v;
    m.clear();
    REQUIRE(m.empty());
    REQUIRE(m.size() == 0);
    REQUIRE(m.begin() == m.end());
}

TEST_CASE("map lower_bound and upper_bound", "[map]") {
    kstd::map<int, int> m;
    for (int v = 0; v <= 20; v += 2) m[v] = v;

    REQUIRE(m.lower_bound(4)->first == 4);
    REQUIRE(m.upper_bound(4)->first == 6);
    REQUIRE(m.lower_bound(5)->first == 6);
    REQUIRE(m.upper_bound(20) == m.end());
}

TEST_CASE("map with non-trivial type tracks destructor calls", "[map]") {
    static int live_count = 0;
    struct Counter {
        Counter() { live_count++; }
        Counter(const Counter &) { live_count++; }
        Counter(Counter &&) noexcept { live_count++; }
        ~Counter() { live_count--; }
    };

    {
        kstd::map<int, Counter> m;
        for (int i = 0; i < 8; i++) {
            m.emplace(i, Counter());
        }
        REQUIRE(live_count == 8);
        m.clear();
        REQUIRE(live_count == 0);
    }
    REQUIRE(live_count == 0);
}
