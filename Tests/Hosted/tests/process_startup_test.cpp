#include <catch2/catch_test_macros.hpp>
#include <kernel/process_startup.hpp>

namespace {
using namespace kernel::process_startup;
using kernel::KernelError;

bool equal(const char *a, const char *b) {
  while (*a && *a == *b) { ++a; ++b; }
  return *a == *b;
}
} // namespace

TEST_CASE("Process startup preserves argv0 and parses quoted arguments", "[process-startup]") {
  Arguments args;
  constexpr char command[] = " \talpha \"two words\" \"\" pre\"mid dle\"post C:\\bin\r\n";
  REQUIRE_FALSE(parse("/System/Test App", 16, command, sizeof(command) - 1, args));
  REQUIRE(args.argc == 6);
  REQUIRE(equal(args.text + args.offsets[0], "/System/Test App"));
  REQUIRE(equal(args.text + args.offsets[1], "alpha"));
  REQUIRE(equal(args.text + args.offsets[2], "two words"));
  REQUIRE(equal(args.text + args.offsets[3], ""));
  REQUIRE(equal(args.text + args.offsets[4], "premid dlepost"));
  REQUIRE(equal(args.text + args.offsets[5], "C:\\bin"));
}

TEST_CASE("Process startup defaults to executable argv0 only", "[process-startup]") {
  Arguments args;
  REQUIRE_FALSE(parse("/init", 5, nullptr, 0, args));
  REQUIRE(args.argc == 1);
  REQUIRE(equal(args.text, "/init"));
  REQUIRE_FALSE(parse("/init", 5, " \t\r\n", 4, args));
  REQUIRE(args.argc == 1);
}

TEST_CASE("Process startup rejects malformed and oversized input", "[process-startup]") {
  Arguments args;
  REQUIRE(parse("", 0, nullptr, 0, args) == KernelError::InvalidArgument);
  REQUIRE(parse(nullptr, 4, nullptr, 0, args) == KernelError::InvalidArgument);
  REQUIRE(parse("a", 1, "\"bad", 4, args) == KernelError::InvalidFormat);
  REQUIRE(parse("a\0b", 3, nullptr, 0, args) == KernelError::InvalidData);
  REQUIRE(parse("a", 1, "x\0y", 3, args) == KernelError::InvalidData);
  REQUIRE(parse("a", 1, nullptr, 1, args) == KernelError::InvalidArgument);
  char path[PATH_CAPACITY];
  for (auto &ch : path) ch = 'p';
  REQUIRE_FALSE(parse(path, PATH_CAPACITY - 1, nullptr, 0, args));
  REQUIRE(parse(path, PATH_CAPACITY, nullptr, 0, args) == KernelError::BufferTooSmall);
  char command[ARGUMENT_CAPACITY];
  for (auto &ch : command) ch = 'x';
  REQUIRE_FALSE(parse("a", 1, command, ARGUMENT_CAPACITY - 1, args));
  REQUIRE_FALSE(parse(path, PATH_CAPACITY - 1, command, ARGUMENT_CAPACITY - 1, args));
  REQUIRE(args.bytes == sizeof(args.text));
  REQUIRE(parse("a", 1, command, ARGUMENT_CAPACITY, args) == KernelError::BufferTooSmall);
}

TEST_CASE("Process startup enforces argument count without truncation", "[process-startup]") {
  Arguments args;
  char command[MAX_ARGUMENTS * 2];
  for (size_t i = 0; i < MAX_ARGUMENTS; ++i) {
    command[i * 2] = 'x';
    command[i * 2 + 1] = ' ';
  }
  REQUIRE_FALSE(parse("a", 1, command, (MAX_ARGUMENTS - 1) * 2, args));
  REQUIRE(args.argc == MAX_ARGUMENTS);
  REQUIRE(parse("a", 1, command, MAX_ARGUMENTS * 2, args) == KernelError::TooManyArguments);
}

TEST_CASE("Process startup builds aligned bounded i386 argc argv stack", "[process-startup]") {
  Arguments args;
  REQUIRE_FALSE(parse("/app", 4, "\"a b\" c", 7, args));
  alignas(16) uint32_t buffer[512]{};
  const auto size = stack_size(args);
  REQUIRE(size % 16 == 0);
  REQUIRE(write_stack(buffer, size - 1, 0x20000, args) == KernelError::BufferTooSmall);
  REQUIRE(write_stack(buffer, sizeof(buffer), 0x20001, args) == KernelError::InvalidAlignment);
  REQUIRE(write_stack(buffer, sizeof(buffer), 0xfffffff0, args) == KernelError::ArithmeticOverflow);
  REQUIRE_FALSE(write_stack(buffer, size, 0x20000, args, 42));
  REQUIRE(buffer[0] == 0);
  REQUIRE(buffer[1] == 0x20010);
  REQUIRE(buffer[2] == 42);
  const auto &info = *reinterpret_cast<const UnDOSStartupInfoV1 *>(buffer + 4);
  REQUIRE(undos::abi::valid_startup(info));
  REQUIRE(info.argc == 3);
  REQUIRE(info.argv == 0x20040);
  REQUIRE(info.envp == 0x20050);
  REQUIRE(buffer[19] == 0);
  REQUIRE(buffer[20] == 0);
  auto *bytes = reinterpret_cast<const char *>(buffer);
  REQUIRE(equal(bytes + buffer[16] - 0x20000, "/app"));
  REQUIRE(equal(bytes + buffer[17] - 0x20000, "a b"));
  REQUIRE(equal(bytes + buffer[18] - 0x20000, "c"));
}
