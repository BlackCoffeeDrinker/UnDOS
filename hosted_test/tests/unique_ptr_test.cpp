#include <memory.hpp>
#include <catch2/catch_test_macros.hpp>

struct TestStruct {
    int value;
    static int count;
    TestStruct(int v = 0) : value(v) { count++; }
    ~TestStruct() { count--; }
};

int TestStruct::count = 0;

TEST_CASE("unique_ptr basic", "[unique_ptr]") {
    TestStruct::count = 0;
    {
        kstd::unique_ptr<TestStruct> p(new TestStruct(42));
        REQUIRE(p->value == 42);
        REQUIRE(TestStruct::count == 1);
    }
    REQUIRE(TestStruct::count == 0);
}

TEST_CASE("make_unique", "[unique_ptr]") {
    TestStruct::count = 0;
    {
        auto p = kstd::make_unique<TestStruct>(10);
        REQUIRE(p->value == 10);
        REQUIRE(TestStruct::count == 1);
    }
    REQUIRE(TestStruct::count == 0);
}

TEST_CASE("unique_ptr array", "[unique_ptr]") {
    TestStruct::count = 0;
    {
        kstd::unique_ptr<TestStruct[]> p(new TestStruct[5]);
        REQUIRE(TestStruct::count == 5);
    }
    REQUIRE(TestStruct::count == 0);
}

TEST_CASE("unique_ptr move", "[unique_ptr]") {
    TestStruct::count = 0;
    {
        auto p1 = kstd::make_unique<TestStruct>(10);
        auto p2 = kstd::move(p1);
        REQUIRE(!p1);
        REQUIRE(p2);
        REQUIRE(p2->value == 10);
        REQUIRE(TestStruct::count == 1);
    }
    REQUIRE(TestStruct::count == 0);
}

TEST_CASE("unique_ptr comparison", "[unique_ptr]") {
    auto p1 = kstd::make_unique<int>(1);
    auto p2 = kstd::make_unique<int>(2);

    REQUIRE(p1 == p1);
    REQUIRE(p1 != p2);
    REQUIRE(!(p1 == nullptr));
    REQUIRE(p1 != nullptr);
    REQUIRE(nullptr == nullptr);

    // operator<=>
    REQUIRE((p1 <=> p2) != 0);
    REQUIRE((p1 <=> nullptr) > 0);
}

TEST_CASE("make_unique_for_overwrite", "[unique_ptr]") {
    {
        auto p = kstd::make_unique_for_overwrite<int>();
        REQUIRE(p);
    }
    {
        auto p = kstd::make_unique_for_overwrite<int[]>(10);
        REQUIRE(p);
    }
}

TEST_CASE("constexpr unique_ptr", "[unique_ptr]") {
    static_assert([]() {
        kstd::unique_ptr<int> p(new int(42));
        return *p == 42;
    }());
}
