
#include "static_string.hpp"
#include "nomm_string.hpp"
#include "string_view.hpp"


#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

TEST_CASE("Static_String == ConstPTR") {
  kstd::static_string<32> a;
  a = "Hello";
  const char* b = "Hello";
  REQUIRE(a == b);
}

TEST_CASE("Static_String substr and find") {
  kstd::static_string<32> a = "Hello World";
  auto sv = a.substr(6, 5);
  REQUIRE(sv == "World");
  REQUIRE(a.find('W') == 6);
  REQUIRE(a.find('z') == kstd::string_view::npos);
}

TEST_CASE("nomm_string basic usage") {
  char buf[32] = "Hello";
  kstd::nomm_string s(buf);
  REQUIRE(s == "Hello");
  s.append(' ');
  s.append("World");
  REQUIRE(s == "Hello World");
  REQUIRE(std::string(buf) == "Hello World");
}

TEST_CASE("String inheritance") {
  kstd::static_string<32> a = "Hello";
  kstd::string_view sv = a.substr();
  REQUIRE(sv == "Hello");
}

TEST_CASE("Static_String == String_View") {
  kstd::static_string<32> a = "Hello";
  kstd::string_view b = "Hello";
  REQUIRE(a == b);
  REQUIRE(b == a);
}

TEST_CASE("Static_String conversion from String_View") {
  kstd::string_view sv = "Hello";
  kstd::static_string<32> s = sv; // implicit construction
  REQUIRE(s == "Hello");
  kstd::static_string<32> s2;
  s2 = sv; // assignment
  REQUIRE(s2 == "Hello");
}

TEST_CASE("Nomm_String == String_View") {
  char buf[32] = "Hello";
  kstd::nomm_string a(buf);
  kstd::string_view b = "Hello";
  REQUIRE(a == b);
  REQUIRE(b == a);
}

TEST_CASE("String push_back") {
  SECTION("static_string") {
    kstd::static_string<32> s;
    s.push_back('H');
    s.push_back('i');
    REQUIRE(s == "Hi");
    REQUIRE(s.length() == 2);
  }

  SECTION("nomm_string") {
    char buf[32] = {0};
    kstd::nomm_string s(buf);
    s.push_back('Y');
    s.push_back('o');
    REQUIRE(s == "Yo");
    REQUIRE(s.length() == 2);
    REQUIRE(std::string(buf) == "Yo");
  }
}

TEST_CASE("String rfind") {
  SECTION("string_view") {
    kstd::string_view sv = "Hello World World";
    REQUIRE(sv.rfind('W') == 12);
    REQUIRE(sv.rfind('o') == 13);
    REQUIRE(sv.rfind("World") == 12);
    REQUIRE(sv.rfind("World", 10) == 6);
    REQUIRE(sv.rfind('z') == kstd::string_view::npos);
    REQUIRE(sv.rfind("Hello", 0) == 0);
  }

  SECTION("static_string") {
    kstd::static_string<32> s = "Hello World World";
    REQUIRE(s.rfind('W') == 12);
    REQUIRE(s.rfind('o') == 13);
    REQUIRE(s.rfind("World") == 12);
    REQUIRE(s.rfind("World", 10) == 6);
    REQUIRE(s.rfind('z') == kstd::string_view::npos);
    REQUIRE(s.rfind("Hello", 0) == 0);
  }

  SECTION("nomm_string") {
    char buf[32] = "Hello World World";
    kstd::nomm_string s(buf);
    REQUIRE(s.rfind('W') == 12);
    REQUIRE(s.rfind('o') == 13);
    REQUIRE(s.rfind("World") == 12);
    REQUIRE(s.rfind("World", 10) == 6);
    REQUIRE(s.rfind('z') == kstd::string_view::npos);
    REQUIRE(s.rfind("Hello", 0) == 0);
  }
}

TEST_CASE("String back and pop_back") {
  SECTION("string_view") {
    kstd::string_view sv = "Hello";
    REQUIRE(sv.back() == 'o');
  }

  SECTION("static_string") {
    kstd::static_string<32> s = "Hello";
    REQUIRE(s.back() == 'o');
    s.pop_back();
    REQUIRE(s == "Hell");
    REQUIRE(s.back() == 'l');
    s.pop_back();
    s.pop_back();
    s.pop_back();
    s.pop_back();
    REQUIRE(s.empty());
  }

  SECTION("nomm_string") {
    char buf[32] = "World";
    kstd::nomm_string s(buf);
    REQUIRE(s.back() == 'd');
    s.pop_back();
    REQUIRE(s == "Worl");
    REQUIRE(std::string(buf) == "Worl");
    s.pop_back();
    s.pop_back();
    s.pop_back();
    s.pop_back();
    REQUIRE(s.empty());
    REQUIRE(buf[0] == '\0');
  }
}
