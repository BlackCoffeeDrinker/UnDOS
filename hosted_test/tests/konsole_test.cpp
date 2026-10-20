#include <kernel/__core.hpp>
#include <kernel/konsole.hpp>
#include <catch2/catch_test_macros.hpp>

namespace {

kstd::static_string<1024> output;
size_t printCalls = 0;

void ResetOutput() {
  output.clear();
  printCalls = 0;
}

}// namespace

void KE_PrintBuffer(const char *str, size_t len) noexcept {
  output.append(kstd::string_view(str, len));
  ++printCalls;
}

TEST_CASE("KE_Print bypasses formatting without arguments", "[strfmt][konsole]") {
  ResetOutput();

  SECTION("Braces remain literal") {
    KE_Print("plain {} {x}");
    REQUIRE(output == "plain {} {x}");
  }

  SECTION("Arrays stop at an embedded NUL") {
    const char message[] = {'a', '\0', 'b'};
    KE_Print(message);
    REQUIRE(output == "a");
  }

  SECTION("Unterminated arrays use their bounds") {
    const char message[] = {'a', 'b', 'c'};
    KE_Print(message);
    REQUIRE(output == "abc");
  }

  SECTION("Dynamic strings bypass the parser") {
    const char *message = "dynamic {}";
    KE_Print(message);
    REQUIRE(output == "dynamic {}");
  }

  SECTION("Explicit views preserve embedded NUL bytes") {
    const char message[] = {'a', '\0', 'b'};
    KE_Print(kstd::string_view(message, sizeof(message)));
    REQUIRE(output.size() == sizeof(message));
    REQUIRE(output[1] == '\0');
    REQUIRE(output[2] == 'b');
  }

  SECTION("Plain messages are not limited by the formatting buffer") {
    char message[300];
    for (char &c: message) c = 'a';
    KE_Print(message);
    REQUIRE(output.size() == sizeof(message));
    REQUIRE(output[299] == 'a');
  }

  REQUIRE(printCalls == 1);
}

TEST_CASE("KE_Print retains bounded formatting behavior", "[strfmt][konsole]") {
  ResetOutput();

  SECTION("Mixed arguments") {
    KE_Print("{} {} {x} {}", "device", 42, 255u, true);
    REQUIRE(output == "device 42 ff True");
  }

  SECTION("Custom buffer with room remaining") {
    KE_Print<8>("{}", "1234567");
    REQUIRE(output == "1234567");
  }

  SECTION("Exact fit does not add an ellipsis") {
    KE_Print<8>("{}", "12345678");
    REQUIRE(output == "12345678");
  }

  SECTION("Overflow replaces the last three bytes") {
    KE_Print<8>("{}", "123456789");
    REQUIRE(output == "12345...");
  }

  SECTION("Smallest buffer") {
    KE_Print<4>("{}", "12345");
    REQUIRE(output == "1...");
  }

  SECTION("Default buffer truncates at 256 bytes") {
    char message[300];
    for (char &c: message) c = 'a';
    KE_Print("{}", kstd::string_view(message, sizeof(message)));
    REQUIRE(output.size() == 256);
    REQUIRE(output[252] == 'a');
    REQUIRE(output.substr(253) == "...");
  }

  SECTION("Empty formatted output is still emitted once") {
    KE_Print("{}", "");
    REQUIRE(output.empty());
  }

  REQUIRE(printCalls == 1);
}
