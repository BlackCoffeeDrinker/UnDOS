#include "gfx/font/vm.hpp"
#include "gfx/font/vm_opcodes.hpp"
#include <catch2/catch_test_macros.hpp>
#include <vector.hpp>

namespace {
using namespace kernel::font::vm;
using Program = kstd::vector<uint8_t>;

struct Machine {
  kstd::vector<int32_t> stack;
  kstd::vector<uint8_t> boundaries;
  Result result{};
  explicit Machine(uint32_t capacity = 64) : stack(capacity) {}
  kstd::error_code run(const Program &program, uint32_t work = MaxWork) {
    boundaries.resize(program.size() + 1);
    result = execute({program.data(), program.size()},
                     {stack.data(), stack.size()},
                     {boundaries.data(), boundaries.size()}, work);
    return result.error;
  }
  kstd::error_code run(const Program &program, State &state, uint32_t work = MaxWork,
            Phase phase = Phase::Glyph) {
    // Retained functions can be longer than the current glyph program.
    boundaries.resize(undos::font::MaxProgramBytes + 1);
    result = execute({program.data(), program.size()}, {stack.data(), stack.size()},
                     {boundaries.data(), boundaries.size()}, state, work, phase);
    return result.error;
  }
};

void word(Program &program, int32_t value) {
  program.push_back(0xb8);
  program.push_back(uint8_t(uint32_t(value) >> 8));
  program.push_back(uint8_t(value));
}

// Construct signed 32-bit operands with word pushes and TT arithmetic.
void integer(Program &program, int32_t value) {
  if (value >= -32768 && value <= 32767) {
    word(program, value);
    return;
  }
  const int32_t high = int32_t(int64_t(value) / 65536 - (value % 65536 < 0));
  const uint32_t low = uint32_t(int64_t(value) - int64_t(high) * 65536);
  word(program, high);
  for (uint32_t i = 0; i < 16; ++i) {
    program.push_back(0x20); program.push_back(0x60);
  }
  word(program, int32_t(low >> 8));
  word(program, 16384);
  program.push_back(0x63);
  word(program, int32_t(low & 255));
  program.push_back(0x60); program.push_back(0x60);
}
}

TEST_CASE("font VM decodes every push encoding and signed big-endian words", "[font][fontvm]") {
  Machine machine(256);
  for (const uint32_t op : {0x40u, 0x41u, 0xb0u, 0xb7u, 0xb8u, 0xbfu}) {
    for (const uint32_t count : {0u, 1u, 8u, 255u}) {
      if (op >= 0xb0 && count != (op & 7) + 1) continue;
      Program program{uint8_t(op)};
      if (op == 0x40 || op == 0x41) program.push_back(uint8_t(count));
      const bool words = op == 0x41 || op >= 0xb8;
      for (uint32_t i = 0; i < count; ++i) {
        program.push_back(0xff);
        if (words) program.push_back(0xd2);
      }
      REQUIRE(machine.run(program) == Error::None);
      REQUIRE(machine.result.stackSize == count);
      for (uint32_t i = 0; i < count; ++i) REQUIRE(machine.stack[i] == (words ? -46 : 255));
      for (size_t length = 1; length < program.size(); ++length) {
        Program truncated(length);
        for (size_t i = 0; i < length; ++i) truncated[i] = program[i];
        REQUIRE(machine.run(truncated) == Error::TruncatedInstruction);
      }
    }
  }
}

TEST_CASE("font VM stack operators use one-based indices and reset per execution", "[font][fontvm]") {
  Machine machine;
  REQUIRE(machine.run({0xb2, 10, 20, 30, 0x8a}) == Error::None);
  REQUIRE(machine.result.stackSize == 3);
  REQUIRE(machine.stack[0] == 20);
  REQUIRE(machine.stack[1] == 30);
  REQUIRE(machine.stack[2] == 10);
  REQUIRE(machine.run({0xb2, 10, 20, 30, 0xb0, 3, 0x25}) == Error::None);
  REQUIRE(machine.result.stackSize == 4);
  REQUIRE(machine.stack[3] == 10);
  REQUIRE(machine.run({0xb2, 10, 20, 30, 0xb0, 3, 0x26}) == Error::None);
  REQUIRE(machine.result.stackSize == 3);
  REQUIRE(machine.stack[0] == 20);
  REQUIRE(machine.stack[1] == 30);
  REQUIRE(machine.stack[2] == 10);
  REQUIRE(machine.run({0xb1, 10, 20, 0x23, 0x20, 0x21, 0x24}) == Error::None);
  REQUIRE(machine.result.stackSize == 3);
  REQUIRE(machine.stack[0] == 20);
  REQUIRE(machine.stack[1] == 10);
  REQUIRE(machine.stack[2] == 2);
  REQUIRE(machine.run({0xb0, 10, 0x22, 0x24}) == Error::None);
  REQUIRE(machine.result.stackSize == 1);
  REQUIRE(machine.stack[0] == 0);
  REQUIRE(machine.run({0x24}) == Error::None);
  REQUIRE(machine.stack[0] == 0);
}

TEST_CASE("font VM rejects stack underflow overflow and invalid indices", "[font][fontvm]") {
  Machine machine(1);
  for (const int op : {0x20, 0x21, 0x23, 0x25, 0x26, 0x58, 0x5c,
                       0x60, 0x64, 0x1c, 0x78, 0x79, 0x8a}) {
    Program program{uint8_t(op)};
    if (op == 0x58) program.push_back(0x59);
    REQUIRE(machine.run(program) == Error::StackUnderflow);
    REQUIRE(machine.result.stackSize == 0);
  }
  REQUIRE(machine.run({0xb1, 1, 2}) == Error::StackOverflow);
  REQUIRE(machine.run({0xb0, 1, 0x20}) == Error::StackOverflow);
  for (const int op : {0x25, 0x26}) {
    for (const int32_t index : {-1, 0, 1, 2}) {
      Program program;
      word(program, index); program.push_back(uint8_t(op));
      REQUIRE(machine.run(program) == Error::InvalidStackIndex);
    }
  }
}

TEST_CASE("font VM numeric operations preserve signed 26.6 semantics", "[font][fontvm]") {
  struct Example { uint8_t opcode; int32_t left, right, expected; };
  const Example examples[] = {
      {0x60, 64, 192, 256}, {0x61, 64, 192, -128},
      {0x62, 64, 192, 21}, {0x62, -64, 192, -21},
      {0x62, 64, -192, -21}, {0x62, -64, -192, 21},
      {0x63, 96, 96, 144}, {0x63, 1, 32, 1}, {0x63, -1, 32, -1},
      {0x63, 1, 31, 0}, {0x63, -1, 31, 0},
      {0x50, -10, 20, 1}, {0x50, 20, -10, 0},
      {0x51, 20, 20, 1}, {0x52, 20, -10, 1}, {0x53, 20, 20, 1},
      {0x54, -10, -10, 1}, {0x55, 20, -10, 1},
      {0x5a, -10, 20, 1}, {0x5a, 0, 20, 0},
      {0x5b, 0, 0, 0}, {0x5b, 0, -10, 1},
      {0x8b, -10, 20, 20}, {0x8c, -10, 20, -10},
  };
  Machine machine;
  for (const auto &example : examples) {
    INFO(unsigned(example.opcode));
    Program program;
    word(program, example.left); word(program, example.right);
    program.push_back(example.opcode);
    REQUIRE(machine.run(program) == Error::None);
    REQUIRE(machine.result.stackSize == 1);
    REQUIRE(machine.stack[0] == example.expected);
  }
  for (int32_t value = -130; value <= 130; ++value) {
    for (const int op : {0x5c, 0x64, 0x65, 0x66, 0x67}) {
      Program program;
      word(program, value); program.push_back(uint8_t(op));
      REQUIRE(machine.run(program) == Error::None);
      const int32_t expected = op == 0x5c ? !value :
          op == 0x64 ? (value < 0 ? -value : value) :
          op == 0x65 ? -value :
          op == 0x66 ? (value >= 0 ? value / 64 * 64 : -((-value + 63) / 64) * 64) :
          (value >= 0 ? (value + 63) / 64 * 64 : -((-value) / 64) * 64);
      REQUIRE(machine.stack[0] == expected);
    }
  }
}

TEST_CASE("font VM detects arithmetic overflow and divide by zero", "[font][fontvm]") {
  Machine machine;
  for (const int32_t value : {INT32_MIN, INT32_MAX, -65536, 65535}) {
    Program program;
    integer(program, value);
    REQUIRE(machine.run(program) == Error::None);
    REQUIRE(machine.stack[0] == value);
  }
  for (const int op : {0x64, 0x65}) {
    Program program;
    integer(program, INT32_MIN); program.push_back(uint8_t(op));
    REQUIRE(machine.run(program) == Error::ArithmeticOverflow);
  }
  struct Example { int32_t left, right; uint8_t opcode; };
  const Example examples[] = {
      {INT32_MAX, 1, 0x60}, {INT32_MIN, 1, 0x61},
      {INT32_MAX, INT32_MAX, 0x63}, {INT32_MIN, INT32_MIN, 0x63},
      {INT32_MIN, -1, 0x62}, {INT32_MAX, 1, 0x62},
      {INT32_MAX, 0, 0x67},
  };
  for (const auto &example : examples) {
    Program program;
    integer(program, example.left);
    if (example.opcode != 0x67) integer(program, example.right);
    program.push_back(example.opcode);
    REQUIRE(machine.run(program) == Error::ArithmeticOverflow);
    REQUIRE(machine.result.stackSize == 0);
  }
  REQUIRE(machine.run({0xb1, 64, 0, 0x62}) == Error::DivisionByZero);
  REQUIRE(machine.result.offset == 3);
  REQUIRE(machine.result.opcode == 0x62);
}

TEST_CASE("font VM skips nested branches without interpreting push data", "[font][fontvm]") {
  Machine machine;
  // The skipped PUSH contains IF/ELSE/EIF bytes, not control instructions.
  REQUIRE(machine.run({0xb0, 0, 0x58, 0xb2, 0x58, 0x1b, 0x59,
                       0xb0, 0, 0x58, 0x90, 0x59, 0x1b, 0xb0, 42, 0x59}) == Error::None);
  REQUIRE(machine.result.stackSize == 1);
  REQUIRE(machine.stack[0] == 42);
  REQUIRE(machine.run({0xb0, 1, 0x58, 0xb0, 42, 0x1b, 0x90, 0x59}) == Error::None);
  REQUIRE(machine.stack[0] == 42);
  REQUIRE(machine.run({0xb0, 0, 0x58, 0x90, 0x59, 0xb0, 7}) == Error::None);
  REQUIRE(machine.stack[0] == 7);
  for (const Program &program : {Program{0x1b}, Program{0x59}, Program{0x58},
                               Program{0x58, 0x1b, 0x1b, 0x59}}) {
    REQUIRE(machine.run(program) == Error::InvalidConditional);
  }
  Program nested;
  for (uint32_t i = 0; i < MaxNesting + 1; ++i) nested.push_back(0x58);
  for (uint32_t i = 0; i < MaxNesting + 1; ++i) nested.push_back(0x59);
  REQUIRE(machine.run(nested) == Error::InvalidConditional);
}

TEST_CASE("font VM jumps relative to opcode and only to instruction boundaries", "[font][fontvm]") {
  Machine machine;
  REQUIRE(machine.run({0xb0, 3, 0x1c, 0xb0, 99, 0xb0, 42}) == Error::None);
  REQUIRE(machine.stack[0] == 42);
  for (const int op : {0x78, 0x79}) {
    const uint8_t condition = op == 0x78 ? 1 : 0;
    REQUIRE(machine.run({0xb1, 3, condition, uint8_t(op), 0xb0, 99, 0xb0, 42}) == Error::None);
    REQUIRE(machine.result.stackSize == 1);
    REQUIRE(machine.stack[0] == 42);
    REQUIRE(machine.run({0xb1, 3, uint8_t(!condition), uint8_t(op), 0xb0, 99, 0xb0, 42}) == Error::None);
    REQUIRE(machine.result.stackSize == 2);
    REQUIRE(machine.stack[0] == 99);
  }
  REQUIRE(machine.run({0xb0, 1, 0x1c}) == Error::None); // Jump to program end.
  REQUIRE(machine.run({0xb0, 2, 0x1c, 0xb0, 99}) == Error::InvalidJump);
  REQUIRE(machine.run({0xb0, 4, 0x1c}) == Error::InvalidJump);
  REQUIRE(machine.run({0xb8, 0xff, 0xfc, 0x1c}) == Error::InvalidJump);
}

TEST_CASE("font VM bounds loops decoding skipped instructions and stack moves", "[font][fontvm]") {
  Machine machine;
  REQUIRE(machine.run({0xb8, 0xff, 0xfd, 0x1c}, 100) == Error::WorkLimit);
  REQUIRE(machine.result.work <= 100);
  REQUIRE(machine.result.instructions > 2);
  REQUIRE(machine.run({0xb0, 0, 0x1c}, 100) == Error::StackUnderflow);
  REQUIRE(machine.result.work < 100);
  REQUIRE(machine.run({0xb0, 1}, 3) == Error::WorkLimit);
  REQUIRE(machine.run({0xb0, 1}, 4) == Error::None);
  REQUIRE(machine.result.work == 4);
  REQUIRE(machine.run({0xb0, 0, 0x58, 0xb0, 99, 0x59}, 10) == Error::WorkLimit);
  REQUIRE(machine.run({0xb2, 1, 2, 2, 0x26}, 11) == Error::WorkLimit);
  REQUIRE(machine.run({0xb0, 1}) == Error::None); // Failure does not poison reuse.
}

TEST_CASE("font VM validates workspace and reports unsupported instructions", "[font][fontvm]") {
  Machine machine;
  REQUIRE(machine.run({0x90}) == Error::UnsupportedInstruction);
  REQUIRE(machine.result.opcode == 0x90);
  REQUIRE(machine.result.offset == 0);
  REQUIRE(machine.run({}) == Error::None);
  REQUIRE(machine.run({}, 0) == Error::InvalidWorkspace);
  REQUIRE(machine.run({}, MaxWork + 1) == Error::InvalidWorkspace);
  uint8_t boundary[2]{};
  const uint8_t code[] = {0x22};
  REQUIRE(execute({nullptr, 1}, {}, {boundary, 2}, 10).error == Error::InvalidWorkspace);
  REQUIRE(execute({code, 1}, {}, {}, 10).error == Error::InvalidWorkspace);
  REQUIRE(execute({code, 1}, {}, {boundary, 1}, 10).error == Error::InvalidWorkspace);
  REQUIRE(execute({code, 1}, {nullptr, 1}, {boundary, 2}, 10).error == Error::InvalidWorkspace);
  REQUIRE(execute({code, 1}, {}, {boundary, 2}, 10).error == Error::None);
  Machine oversized(MaxStack + 1);
  REQUIRE(oversized.run({}) == Error::InvalidWorkspace);
  Program tooLong(undos::font::MaxProgramBytes + 1);
  REQUIRE(machine.run(tooLong) == Error::InvalidWorkspace);
}

TEST_CASE("font VM single-byte programs fail explicitly or complete within budget", "[font][fontvm]") {
  Machine machine;
  for (uint32_t op = 0; op <= 255; ++op) {
    const auto error = machine.run({uint8_t(op)}, 32);
    REQUIRE(machine.result.work <= 32);
    REQUIRE(machine.result.stackSize <= machine.stack.size());
    REQUIRE(!error.message().empty());
    if (error) REQUIRE(&error.category() == &font_vm_category());
    if (!error) REQUIRE((op == kernel::font::vm::opcode::CLEAR || op == kernel::font::vm::opcode::DEPTH));
  }
}

TEST_CASE("font VM honors maximum program and conditional boundaries", "[font][fontvm]") {
  Machine machine;
  Program longest(undos::font::MaxProgramBytes, uint8_t(0x22));
  REQUIRE(machine.run(longest) == Error::None);
  REQUIRE(machine.result.instructions == undos::font::MaxProgramBytes);
  REQUIRE(machine.result.work == undos::font::MaxProgramBytes * 2);
  Program nested{0x40, uint8_t(MaxNesting)};
  for (uint32_t i = 0; i < MaxNesting; ++i) nested.push_back(1);
  for (uint32_t i = 0; i < MaxNesting; ++i) nested.push_back(0x58);
  for (uint32_t i = 0; i < MaxNesting; ++i) nested.push_back(0x59);
  REQUIRE(machine.run(nested) == Error::None);
  REQUIRE(machine.result.stackSize == 0);
  Machine full(MaxStack);
  Program fill;
  for (uint32_t remaining = MaxStack; remaining;) {
    const uint32_t count = remaining < 255 ? remaining : 255;
    fill.push_back(0x40); fill.push_back(uint8_t(count));
    for (uint32_t i = 0; i < count; ++i) fill.push_back(1);
    remaining -= count;
  }
  REQUIRE(full.run(fill) == Error::None);
  REQUIRE(full.result.stackSize == MaxStack);
  fill.push_back(0x20);
  REQUIRE(full.run(fill) == Error::StackOverflow);
}

TEST_CASE("font VM mutated programs preserve scratch canaries and bounded execution", "[font][fontvm]") {
  int32_t stack[66]{};
  uint8_t boundaries[130]{};
  stack[0] = 123456; stack[65] = -123456;
  boundaries[0] = 0xa5; boundaries[129] = 0x5a;
  uint32_t seed = 0x55465431;
  for (uint32_t trial = 0; trial < 4096; ++trial) {
    Program program(1 + trial % 127);
    for (auto &byte : program) {
      seed = seed * 1664525u + 1013904223u;
      byte = uint8_t(seed >> 24);
    }

    const auto result = execute({program.data(), program.size()},
                                {stack + 1, 64}, {boundaries + 1, 128}, 512);
    REQUIRE(result.work <= 512);
    REQUIRE(result.stackSize <= 64);
    REQUIRE(stack[0] == 123456);
    REQUIRE(stack[65] == -123456);
    REQUIRE(boundaries[0] == 0xa5);
    REQUIRE(boundaries[129] == 0x5a);
    REQUIRE(result.offset <= program.size());
  }
}

namespace {
    struct Memory {
      int32_t storage[4]{};
      uint8_t written[4]{};
      int32_t cvt[3]{};
      uint8_t source[12]{};
      State state{{storage, 4}, {written, 4}, {cvt, 3}};
      Memory() {
        undos::font::write_u32(source, 750);
        undos::font::write_u32(source + 4, uint32_t(-250));
        undos::font::write_u32(source + 8, 1);
      }
      kstd::error_code reset(uint32_t ppem = 16, uint32_t units = 1000) {
        return initialize(state, {source, sizeof(source)}, ppem, units);
      }
    };
    }// namespace

    TEST_CASE("font VM initializes signed CVT and retains storage and CVT across runs", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      REQUIRE(memory.reset() == Error::None);
      REQUIRE(memory.cvt[0] == 768);
      REQUIRE(memory.cvt[1] == -256);
      REQUIRE(memory.cvt[2] == 1);
      for (uint32_t i = 0; i < 4; ++i) {
        REQUIRE(memory.storage[i] == 0);
        REQUIRE(memory.written[i] == 0);
      }
      REQUIRE(machine.run({0xb1, 3, 42, 0x42, 0xb0, 3, 0x43}, memory.state) == Error::None);
      REQUIRE(machine.result.stackSize == 1);
      REQUIRE(machine.stack[0] == 42);
      REQUIRE(machine.run({0xb0, 3, 0x43}, memory.state) == Error::None);
      REQUIRE(machine.stack[0] == 42);
      REQUIRE(machine.run({0xb1, 1, 64, 0x44, 0xb0, 1, 0x45}, memory.state) == Error::None);
      REQUIRE(machine.stack[0] == 64);
      REQUIRE(machine.run({0xb0, 1, 0x45}, memory.state) == Error::None);
      REQUIRE(machine.stack[0] == 64);
      REQUIRE(memory.reset(32) == Error::None);
      REQUIRE(memory.cvt[0] == 1536);
      REQUIRE(memory.cvt[1] == -512);
      REQUIRE(memory.storage[3] == 0);
      REQUIRE(memory.written[3] == 0);
      REQUIRE(machine.run({0xb0, 3, 0x43}, memory.state) == Error::UninitializedStorage);
      REQUIRE_FALSE(memory.state.ready);
    }

    TEST_CASE("font VM scales WCVTF and reports configured ppem and 96 DPI point size", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      for (const uint32_t ppem: {1u, 16u, 256u}) {
        REQUIRE(memory.reset(ppem) == Error::None);
        REQUIRE(machine.run({0x4b, 0x4c}, memory.state) == Error::None);
        REQUIRE(machine.result.stackSize == 2);
        REQUIRE(machine.stack[0] == int32_t(ppem));
        REQUIRE(machine.stack[1] == int32_t(ppem * 48));
      }
      REQUIRE(memory.reset(1, 128) == Error::None);
      REQUIRE(machine.run({0xb9, 0, 2, 0xff, 0xff, 0x70, 0xb0, 2, 0x45}, memory.state) == Error::None);
      REQUIRE(machine.stack[0] == -1);
      REQUIRE(machine.run({0xb1, 2, 1, 0x70, 0xb0, 2, 0x45}, memory.state) == Error::None);
      REQUIRE(machine.stack[0] == 1);
      Memory other;
      REQUIRE(other.reset(32) == Error::None);
      REQUIRE(machine.run({0xb0, 2, 0x45}, other.state) == Error::None);
      REQUIRE(machine.stack[0] == 2);
      REQUIRE(memory.cvt[2] == 1);
    }

    TEST_CASE("font VM bounds storage and CVT reads and writes without modifying adjacent entries", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      for (const int op: {0x42, 0x43, 0x44, 0x45, 0x70}) {
        const bool storage = op == 0x42 || op == 0x43;
        const bool read = op == 0x43 || op == 0x45;
        for (const int32_t index: {-1, storage ? 4 : 3, INT32_MAX, INT32_MIN}) {
          REQUIRE(memory.reset() == Error::None);
          Program program;
          integer(program, index);
          if (!read) word(program, 99);
          program.push_back(uint8_t(op));
          REQUIRE(machine.run(program, memory.state) ==
                  (storage ? Error::InvalidStorageIndex : Error::InvalidCvtIndex));
          REQUIRE(machine.result.stackSize == 0);
          REQUIRE_FALSE(memory.state.ready);
          REQUIRE(memory.storage[0] == 0);
          REQUIRE(memory.storage[3] == 0);
          REQUIRE(memory.cvt[0] == 768);
          REQUIRE(memory.cvt[2] == 1);
        }
      }
      REQUIRE(memory.reset() == Error::None);
      REQUIRE(machine.run({0xb0, 0, 0x43}, memory.state) == Error::UninitializedStorage);
      REQUIRE(machine.run({}, memory.state) == Error::InvalidState);
    }

    TEST_CASE("font VM invalidates partial state on every failure and requires explicit reset", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      REQUIRE(machine.run({}, memory.state) == Error::InvalidState);
      REQUIRE(memory.reset() == Error::None);
      REQUIRE(machine.run({0xb1, 0, 99, 0x42, 0x90}, memory.state) == Error::UnsupportedInstruction);
      REQUIRE(memory.storage[0] == 99);// Not rolled back; state is unusable.
      REQUIRE_FALSE(memory.state.ready);
      REQUIRE(machine.run({0xb0, 0, 0x43}, memory.state) == Error::InvalidState);
      REQUIRE(memory.reset() == Error::None);
      REQUIRE(memory.storage[0] == 0);
      REQUIRE(memory.written[0] == 0);
      REQUIRE(machine.run({0xb0, 0, 0x45}, memory.state, 1) == Error::WorkLimit);
      REQUIRE_FALSE(memory.state.ready);
      REQUIRE(memory.reset(256, 16) == Error::None);
      Program program{0xb0, 0};
      integer(program, INT32_MAX);
      program.push_back(0x70);
      const int32_t previous = memory.cvt[0];
      REQUIRE(machine.run(program, memory.state) == Error::ArithmeticOverflow);
      REQUIRE(memory.cvt[0] == previous);
      REQUIRE_FALSE(memory.state.ready);
    }

    TEST_CASE("font VM validates state initialization buffers and scale", "[font][fontvm]") {
      Memory memory;
      REQUIRE(memory.reset(0) == Error::InvalidState);
      REQUIRE(memory.reset(257) == Error::InvalidState);
      REQUIRE(memory.reset(16, 15) == Error::InvalidState);
      REQUIRE(memory.reset(16, 16385) == Error::InvalidState);
      REQUIRE(initialize(memory.state, {nullptr, 12}, 16, 1000) == Error::InvalidWorkspace);
      REQUIRE(initialize(memory.state, {memory.source, 11}, 16, 1000) == Error::InvalidWorkspace);
      undos::font::write_u32(memory.source, uint32_t(INT32_MAX));
      REQUIRE(memory.reset(256, 16) == Error::ArithmeticOverflow);
      REQUIRE_FALSE(memory.state.ready);
      State empty{};
      REQUIRE(initialize(empty, {}, 16, 1000) == Error::None);
      Machine machine;
      REQUIRE(machine.run({0x4b}, empty) == Error::None);
      REQUIRE(machine.run({0xb0, 0, 0x45}, empty) == Error::InvalidCvtIndex);
      State bad{{memory.storage, 4}, {memory.written, 3}, {memory.cvt, 3}};
      REQUIRE(initialize(bad, {memory.source, 12}, 16, 1000) == Error::InvalidWorkspace);
      bad = {{nullptr, 1}, {memory.written, 1}, {}};
      REQUIRE(initialize(bad, {}, 16, 1000) == Error::InvalidWorkspace);
      bad = {{}, {}, {nullptr, 1}};
      REQUIRE(initialize(bad, {memory.source, 4}, 16, 1000) == Error::InvalidWorkspace);
      bad = {{memory.storage, MaxStorage + 1}, {memory.written, MaxStorage + 1}, {}};
      REQUIRE(initialize(bad, {}, 16, 1000) == Error::InvalidWorkspace);
      bad = {{}, {}, {memory.cvt, MaxCvt + 1}};
      REQUIRE(initialize(bad, {}, 16, 1000) == Error::InvalidWorkspace);
      REQUIRE(machine.run({0x4b}) == Error::InvalidState);
      REQUIRE(machine.run({0x4c}) == Error::InvalidState);
      REQUIRE(machine.run({0xb1, 0, 1, 0x42}) == Error::InvalidState);
    }

    TEST_CASE("font VM stateful opcodes enforce stack requirements", "[font][fontvm]") {
      Memory memory;
      Machine machine(1);
      for (const int op : {0x42, 0x43, 0x44, 0x45, 0x70}) {
        REQUIRE(memory.reset() == Error::None);
        REQUIRE(machine.run({uint8_t(op)}, memory.state) == Error::StackUnderflow);
        REQUIRE_FALSE(memory.state.ready);
      }
      Machine empty(0);
      for (const int op : {0x4b, 0x4c}) {
        REQUIRE(memory.reset() == Error::None);
        REQUIRE(empty.run({uint8_t(op)}, memory.state) == Error::StackOverflow);
        REQUIRE_FALSE(memory.state.ready);
      }
      REQUIRE(memory.reset() == Error::None);
      memory.state.ppem = 0;
      REQUIRE(machine.run({}, memory.state) == Error::InvalidState);
    }

    TEST_CASE("font VM supports declared storage and CVT capacity boundaries", "[font][fontvm]") {
      kstd::vector<int32_t> storage(MaxStorage), cvt(MaxCvt);
      kstd::vector<uint8_t> written(MaxStorage), source(MaxCvt * 4);
      State state{{storage.data(), storage.size()}, {written.data(), written.size()},
                  {cvt.data(), cvt.size()}};
      REQUIRE(initialize(state, {source.data(), source.size()}, 16, 1000) == Error::None);
      Machine machine;
      Program program;
      integer(program, MaxStorage - 1); word(program, -123); program.push_back(0x42);
      integer(program, MaxStorage - 1); program.push_back(0x43);
      REQUIRE(machine.run(program, state) == Error::None);
      REQUIRE(machine.stack[0] == -123);
      program.clear();
      integer(program, MaxCvt - 1); word(program, -456); program.push_back(0x44);
      integer(program, MaxCvt - 1); program.push_back(0x45);
      REQUIRE(machine.run(program, state) == Error::None);
      REQUIRE(machine.stack[0] == -456);
    }

    TEST_CASE("font VM mutated stateful programs preserve state buffer canaries", "[font][fontvm]") {
      int32_t storage[6]{}, cvt[5]{};
      uint8_t written[6]{}, source[12]{};
      storage[0] = 123; storage[5] = 456;
      cvt[0] = 789; cvt[4] = -123;
      written[0] = 0xa5; written[5] = 0x5a;
      State state{{storage + 1, 4}, {written + 1, 4}, {cvt + 1, 3}};
      Machine machine;
      uint32_t seed = 0x434f4e54;
      for (uint32_t trial = 0; trial < 2048; ++trial) {
        REQUIRE(initialize(state, {source, sizeof(source)}, 16, 1000) == Error::None);
        Program program(1 + trial % 63);
        for (auto &byte : program) {
          seed = seed * 1664525u + 1013904223u;
          byte = uint8_t(seed >> 24);
        }
        const auto error = machine.run(program, state, 256);
        REQUIRE(state.ready == (error == Error::None));
        REQUIRE(machine.result.work <= 256);
        REQUIRE(storage[0] == 123);
        REQUIRE(storage[5] == 456);
        REQUIRE(cvt[0] == 789);
        REQUIRE(cvt[4] == -123);
        REQUIRE(written[0] == 0xa5);
        REQUIRE(written[5] == 0x5a);
      }
    }

    namespace {
    struct Functions {
      Function definitions[4]{};
      State state{};
      Functions() {
        state.functions = {definitions, 4};
        reset();
      }
      void reset() {
        REQUIRE(initialize(state, {}, 16, 1000) == Error::None);
      }
    };
    }

    TEST_CASE("font VM defines persistent functions and shares the operand stack through calls", "[font][fontvm]") {
      Functions functions;
      Machine machine;
      const Program font{0xb0, 0, 0x2c, 0xb0, 3, 0x60, 0x2d,
                         0xb0, 1, 0x2c, 0xb0, 0, 0x2b, 0x20, 0x2d};
      REQUIRE(machine.run(font, functions.state, MaxWork, Phase::Font) == Error::None);
      REQUIRE(machine.result.stackSize == 0);
      REQUIRE(functions.definitions[0].defined);
      REQUIRE(functions.definitions[1].defined);
      REQUIRE(machine.run({0xb1, 7, 1, 0x2b, 0x60}, functions.state) == Error::None);
      REQUIRE(machine.result.stackSize == 1);
      REQUIRE(machine.stack[0] == 20);
      REQUIRE(machine.run({0xb2, 1, 4, 0, 0x2a}, functions.state) == Error::None);
      REQUIRE(machine.stack[0] == 13); // 1 + four calls adding 3.
      REQUIRE(machine.run({0xb2, 1, 0, 0, 0x2a}, functions.state) == Error::None);
      REQUIRE(machine.stack[0] == 1);
      const Program prep{0xb0, 0, 0x2c, 0xb0, 5, 0x60, 0x2d};
      REQUIRE(machine.run(prep, functions.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(machine.run({0xb1, 7, 0, 0x2b}, functions.state) == Error::None);
      REQUIRE(machine.stack[0] == 12);
      functions.reset();
      REQUIRE_FALSE(functions.definitions[0].defined);
      REQUIRE(machine.run({0xb0, 0, 0x2b}, functions.state) == Error::InvalidFunction);
    }

    TEST_CASE("font VM handles empty bodies push data and branch-local definitions", "[font][fontvm]") {
      Functions functions;
      Machine machine;
      const Program font{
          0xb0, 0, 0x2c, 0x2d,
          0xb0, 1, 0x2c, 0xb2, 0x2d, 0x2c, 0x58, 0x2d,
          0xb0, 1, 0x58, 0xb0, 2, 0x2c, 0xb0, 9, 0x2d, 0x1b, 0x90, 0x59};
      REQUIRE(machine.run(font, functions.state, MaxWork, Phase::Font) == Error::None);
      REQUIRE(machine.run({0xb0, 0, 0x2b, 0xb0, 2, 0x2b}, functions.state) == Error::None);
      REQUIRE(machine.result.stackSize == 1);
      REQUIRE(machine.stack[0] == 9);
      REQUIRE(machine.run({0xb0, 1, 0x2b}, functions.state) == Error::None);
      REQUIRE(machine.result.stackSize == 3);
      REQUIRE(machine.stack[0] == 0x2d);
      REQUIRE(machine.stack[1] == 0x2c);
      REQUIRE(machine.stack[2] == 0x58);
    }

    TEST_CASE("font VM rejects malformed function definitions and invalid function operands", "[font][fontvm]") {
      Functions functions;
      Machine machine;
      for (const Program &program : {Program{0xb0, 0, 0x2c}, Program{0x2d},
          Program{0xb0, 0, 0x2c, 0x2c, 0x2d, 0x2d},
          Program{0xb0, 0, 0x2c, 0x58, 0x2d}}) {
        functions.reset();
        REQUIRE(machine.run(program, functions.state, MaxWork, Phase::Font) == Error::InvalidDefinition);
      }
      for (const int32_t id : {-1, 4, INT32_MAX}) {
        functions.reset();
        Program font;
        integer(font, id); font.push_back(0x2c); font.push_back(0x2d);
        REQUIRE(machine.run(font, functions.state, MaxWork, Phase::Font) == Error::InvalidFunction);
        functions.reset();
        Program call;
        integer(call, id); call.push_back(0x2b);
        REQUIRE(machine.run(call, functions.state) == Error::InvalidFunction);
      }
      const Program font{0xb0, 0, 0x2c, 0x2d};
      functions.reset();
      REQUIRE(machine.run(font, functions.state, MaxWork, Phase::Font) == Error::None);
      REQUIRE(machine.run({0xb9, 0xff, 0xff, 0, 0, 0x2a}, functions.state) == Error::InvalidRepeatCount);
      functions.reset();
      REQUIRE(machine.run({0x2b}, functions.state) == Error::StackUnderflow);
      functions.reset();
      REQUIRE(machine.run({0x2c, 0x2d}, functions.state, MaxWork, Phase::Font) == Error::StackUnderflow);
    }

    TEST_CASE("font VM glyph phase cannot define functions directly or inside called code", "[font][fontvm]") {
      Functions functions;
      Machine machine;
      const Program font{0xb0, 0, 0x2c, 0x2d};
      REQUIRE(machine.run(font, functions.state) == Error::DefinitionInGlyph);
      functions.reset();
      REQUIRE(machine.run(font, functions.state, MaxWork, Phase::Font) == Error::None);
      // Even a kernel-provided body containing a definition keeps its caller phase.
      const uint8_t definingBody[] = {0xb0, 1, 0x2c, 0x2d};
      functions.definitions[0] = {{definingBody, sizeof(definingBody)}, true};
      REQUIRE(machine.run({0xb0, 0, 0x2b}, functions.state) == Error::DefinitionInGlyph);
      REQUIRE_FALSE(functions.state.ready);
    }

    TEST_CASE("font VM bounds recursive repeated and malformed function calls", "[font][fontvm]") {
      Functions functions;
      Machine machine;
      const Program recursive{0xb0, 0, 0x2c, 0xb0, 0, 0x2b, 0x2d};
      REQUIRE(machine.run(recursive, functions.state, MaxWork, Phase::Font) == Error::None);
      REQUIRE(machine.run({0xb0, 0, 0x2b}, functions.state) == Error::CallLimit);
      functions.reset();
      const Program empty{0xb0, 0, 0x2c, 0x2d};
      REQUIRE(machine.run(empty, functions.state, MaxWork, Phase::Font) == Error::None);
      Program repeat;
      integer(repeat, INT32_MAX); word(repeat, 0); repeat.push_back(0x2a);
      REQUIRE(machine.run(repeat, functions.state, 1000) == Error::WorkLimit);
      REQUIRE(machine.result.work <= 1000);
      functions.reset();
      const Program bad{0xb0, 0, 0x2c, 0x90, 0x2d};
      REQUIRE(machine.run(bad, functions.state, MaxWork, Phase::Font) == Error::None);
      REQUIRE(machine.run({0xb0, 0, 0x2b}, functions.state) == Error::UnsupportedInstruction);
      REQUIRE(machine.result.program.data == bad.data() + 3);
      REQUIRE(machine.result.offset == 0);
      functions.reset();
      functions.definitions[0] = {{nullptr, 1}, true};
      REQUIRE(machine.run({0xb0, 0, 0x2b}, functions.state) == Error::InvalidFunction);
    }

    TEST_CASE("font VM keeps function branches jumps and returns inside the active program", "[font][fontvm]") {
      Functions functions;
      Machine machine;
      const Program font{
          0xb0, 0, 0x2c, 0xb0, 3, 0x1c, 0xb0, 99, 0xb0, 42, 0x2d,
          0xb0, 1, 0x2c, 0x58, 0xb0, 7, 0x1b, 0xb0, 9, 0x59, 0x2d};
      REQUIRE(machine.run(font, functions.state, MaxWork, Phase::Font) == Error::None);
      REQUIRE(machine.run({0xb0, 0, 0x2b, 0xb0, 3, 0x1c, 0xb0, 99, 0xb0, 5, 0x60},
                          functions.state) == Error::None);
      REQUIRE(machine.stack[0] == 47);
      REQUIRE(machine.run({0xb1, 0, 1, 0x2b}, functions.state) == Error::None);
      REQUIRE(machine.stack[0] == 9);
      REQUIRE(machine.run({0xb1, 1, 1, 0x2b}, functions.state) == Error::None);
      REQUIRE(machine.stack[0] == 7);
      const Program escape{0xb0, 0, 0x2c, 0xb0, 100, 0x1c, 0x2d};
      REQUIRE(machine.run(escape, functions.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(machine.run({0xb0, 0, 0x2b}, functions.state) == Error::InvalidJump);
      REQUIRE(machine.result.program.data == escape.data() + 3);
      REQUIRE(machine.result.offset == 2);
    }

    TEST_CASE("font VM checks retained body scratch capacity and function table limits", "[font][fontvm]") {
      Functions functions;
      Machine machine;
      const Program font{0xb0, 0, 0x2c, 0xb7, 1, 2, 3, 4, 5, 6, 7, 8, 0x2d};
      REQUIRE(machine.run(font, functions.state, MaxWork, Phase::Font) == Error::None);
      const uint8_t call[] = {0xb0, 0, 0x2b};
      uint8_t boundaries[4]{};
      int32_t stack[8]{};
      REQUIRE(execute({call, sizeof(call)}, {stack, 8}, {boundaries, 4},
                      functions.state, MaxWork).error == Error::InvalidWorkspace);
      functions.reset();
      functions.state.functions = {nullptr, 1};
      REQUIRE(initialize(functions.state, {}, 16, 1000) == Error::InvalidWorkspace);
      Function definitions[MaxFunctions]{};
      State state{};
      state.functions = {definitions, MaxFunctions};
      REQUIRE(initialize(state, {}, 16, 1000) == Error::None);
      const Program last{0xb0, 255, 0x2c, 0xb0, 42, 0x2d};
      REQUIRE(machine.run(last, state, MaxWork, Phase::Font) == Error::None);
      REQUIRE(machine.run({0xb0, 255, 0x2b}, state) == Error::None);
      REQUIRE(machine.stack[0] == 42);
      state.functions = {definitions, MaxFunctions + 1};
      REQUIRE(initialize(state, {}, 16, 1000) == Error::InvalidWorkspace);
    }

    TEST_CASE("font VM rounds signed distances in all basic grid modes", "[font][fontvm]") {
      struct Example {
        uint8_t opcode;
        int32_t input;
        int32_t expected;
      };
      const Example examples[] = {
          {0x18, 0, 0}, {0x18, 31, 0}, {0x18, 32, 64}, {0x18, -32, -64},
          {0x18, 96, 128}, {0x18, -96, -128},
          {0x19, 0, 32}, {0x19, 63, 32}, {0x19, 64, 96},
          {0x19, -1, -32}, {0x19, -64, -96},
          {0x3d, 15, 0}, {0x3d, 16, 32}, {0x3d, -16, -32},
          {0x7d, 63, 0}, {0x7d, -63, 0}, {0x7d, -65, -64},
          {0x7c, 0, 0}, {0x7c, 1, 64}, {0x7c, -1, -64},
          {0x7a, 31, 31}, {0x7a, -31, -31},
      };
      Memory memory;
      Machine machine;
      REQUIRE(memory.reset() == Error::None);
      for (const auto &example : examples) {
        for (const int color : {0x68, 0x69, 0x6a}) {
          Program program{example.opcode};
          word(program, example.input);
          program.push_back(uint8_t(color));
          REQUIRE(machine.run(program, memory.state) == Error::None);
          REQUIRE(machine.result.stackSize == 1);
          REQUIRE(machine.stack[0] == example.expected);
        }
      }
    }

    TEST_CASE("font VM basic rounding is sign symmetric monotone and quantized", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      REQUIRE(memory.reset() == Error::None);
      for (const int op : {0x18, 0x19, 0x3d, 0x7c, 0x7d, 0x7a}) {
        int32_t previous = 0;
        for (int32_t value = 1; value <= 256; ++value) {
          Program program{uint8_t(op)};
          word(program, value); program.push_back(0x68);
          word(program, -value); program.push_back(0x68);
          REQUIRE(machine.run(program, memory.state) == Error::None);
          REQUIRE(machine.stack[0] == -machine.stack[1]);
          REQUIRE(machine.stack[0] >= previous);
          if (op != 0x7a) {
            const int32_t period = op == 0x3d ? 32 : 64;
            const int32_t phase = op == 0x19 ? 32 : 0;
            REQUIRE((machine.stack[0] - phase) % period == 0);
          }
          previous = machine.stack[0];
        }
      }
    }

    TEST_CASE("font VM ROUND differs from NROUND and ODD EVEN use current rounding", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      REQUIRE(memory.reset() == Error::None);
      for (const int32_t value : {-160, -96, -32, 0, 32, 96, 160}) {
        Program program;
        word(program, value); program.push_back(0x56);
        word(program, value); program.push_back(0x57);
        REQUIRE(machine.run(program, memory.state) == Error::None);
        const int32_t integerValue = (value < 0 ? -value : value) + 32;
        REQUIRE(machine.stack[0] == ((integerValue / 64) % 2));
        REQUIRE(machine.stack[1] == !machine.stack[0]);
        for (const int op : {0x6c, 0x6d, 0x6e}) {
          Program unrounded;
          word(unrounded, value); unrounded.push_back(uint8_t(op));
          REQUIRE(machine.run(unrounded, memory.state) == Error::None);
          REQUIRE(machine.stack[0] == value);
        }
      }
      REQUIRE(machine.run({0x7d, 0xb0, 63, 0x56, 0xb0, 63, 0x57}, memory.state) == Error::None);
      REQUIRE(machine.stack[0] == 0);
      REQUIRE(machine.stack[1] == 1);
    }

    TEST_CASE("font VM saves scalar preparation parameters and restores them for every glyph", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      REQUIRE(memory.reset() == Error::None);
      const Program prep{0x7d, 0x4e,
          0xb0, 42, 0x1d, 0xb0, 12, 0x1e, 0xb8, 0, 250, 0x1f,
          0xb0, 32, 0x1a, 0xb0, 16, 0x5e, 0xb0, 4, 0x5f};
      REQUIRE(machine.run(prep, memory.state, MaxWork, Phase::Prep) == Error::None);
      const auto &saved = memory.state.prepared;
      REQUIRE(saved.round == RoundMode::Down);
      REQUIRE_FALSE(saved.autoFlip);
      REQUIRE(saved.cvtCutIn == 42);
      REQUIRE(saved.singleWidthCutIn == 12);
      REQUIRE(saved.singleWidth == 256);
      REQUIRE(saved.minimumDistance == 32);
      REQUIRE(saved.deltaBase == 16);
      REQUIRE(saved.deltaShift == 4);
      REQUIRE(machine.run({0xb0, 63, 0x68, 0x7c, 0x4d, 0xb0, 99, 0x1d}, memory.state) == Error::None);
      REQUIRE(machine.stack[0] == 0);
      REQUIRE(memory.state.parameters.round == RoundMode::Up);
      REQUIRE(memory.state.parameters.autoFlip);
      REQUIRE(machine.run({0xb0, 63, 0x68}, memory.state) == Error::None);
      REQUIRE(machine.stack[0] == 0);
      REQUIRE(memory.state.parameters.cvtCutIn == 42);
      REQUIRE_FALSE(memory.state.parameters.autoFlip);
      REQUIRE(machine.run({}, memory.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(memory.state.prepared.round == RoundMode::Grid);
      REQUIRE(memory.state.prepared.cvtCutIn == 68);
      REQUIRE(memory.state.prepared.autoFlip);
      REQUIRE(machine.run({0x7c, 0x90}, memory.state, MaxWork, Phase::Prep) == Error::UnsupportedInstruction);
      REQUIRE(memory.state.prepared.round == RoundMode::Grid);
      REQUIRE_FALSE(memory.state.ready);
    }

    TEST_CASE("font VM shares scalar parameters through calls but not across glyphs", "[font][fontvm]") {
      Functions functions;
      Machine machine;
      const Program font{0xb0, 0, 0x2c, 0x7c, 0xb0, 13, 0x1d, 0x2d};
      REQUIRE(machine.run(font, functions.state, MaxWork, Phase::Font) == Error::None);
      REQUIRE(machine.run({0xb0, 0, 0x2b}, functions.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(functions.state.prepared.round == RoundMode::Up);
      REQUIRE(functions.state.prepared.cvtCutIn == 13);
      REQUIRE(machine.run({0xb0, 1, 0x68, 0x7a}, functions.state) == Error::None);
      REQUIRE(machine.stack[0] == 64);
      REQUIRE(machine.run({0xb0, 1, 0x68}, functions.state) == Error::None);
      REQUIRE(machine.stack[0] == 64);
      functions.reset();
      REQUIRE(functions.state.prepared.cvtCutIn == 68);
    }

    TEST_CASE("font VM bounds scalar graphics parameters and rounding overflow", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      for (const int op : {0x1a, 0x1d, 0x1e, 0x1f, 0x5e, 0x5f, 0x56, 0x57, 0x68, 0x6c}) {
        REQUIRE(memory.reset() == Error::None);
        REQUIRE(machine.run({uint8_t(op)}, memory.state) == Error::StackUnderflow);
      }
      for (const int op : {0x6b, 0x6f}) {
        REQUIRE(memory.reset() == Error::None);
        REQUIRE(machine.run({0xb0, 1, uint8_t(op)}, memory.state) == Error::InvalidParameter);
      }
      for (const int32_t value : {-1, 32, INT32_MAX}) {
        REQUIRE(memory.reset() == Error::None);
        Program program;
        integer(program, value); program.push_back(0x5f);
        REQUIRE(machine.run(program, memory.state) == Error::InvalidParameter);
      }
      for (const int op : {0x18, 0x3d, 0x7c}) {
        REQUIRE(memory.reset() == Error::None);
        Program program{uint8_t(op)};
        integer(program, INT32_MAX); program.push_back(0x68);
        REQUIRE(machine.run(program, memory.state) == Error::ArithmeticOverflow);
        REQUIRE_FALSE(memory.state.ready);
      }
      REQUIRE(memory.reset() == Error::None);
      Program minimum;
      integer(minimum, INT32_MIN); minimum.push_back(0x68);
      REQUIRE(machine.run(minimum, memory.state) == Error::None);
      REQUIRE(machine.stack[0] == INT32_MIN);
      REQUIRE(memory.reset() == Error::None);
      Program half{0x19};
      integer(half, INT32_MIN); half.push_back(0x68);
      REQUIRE(machine.run(half, memory.state) == Error::ArithmeticOverflow);
      REQUIRE(memory.reset(256, 16) == Error::None);
      Program width;
      integer(width, INT32_MAX); width.push_back(0x1f);
      REQUIRE(machine.run(width, memory.state) == Error::ArithmeticOverflow);
      REQUIRE(memory.reset() == Error::None);
      for (const int op : {0x1a, 0x1d, 0x1e, 0x1f}) {
        REQUIRE(machine.run({0xb8, 0xff, 0xff, uint8_t(op)}, memory.state) == Error::None);
      }
      REQUIRE(memory.reset() == Error::None);
      REQUIRE(machine.run({0xb0, 31, 0x5f}, memory.state) == Error::None);
      REQUIRE(memory.state.parameters.deltaShift == 31);
      REQUIRE(machine.run({0xb8, 0xff, 0xff, 0x5e}, memory.state) == Error::InvalidParameter);
    }

    TEST_CASE("font VM sets axis vectors and returns signed 2.14 components", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      REQUIRE(memory.reset() == Error::None);
      for (const int op : {0, 1, 2, 3, 4, 5}) {
        REQUIRE(machine.run({uint8_t(op), 0x0c, 0x0d}, memory.state) == Error::None);
        const bool projectionY = op == 0 || op == 2;
        const bool freedomY = op == 0 || op == 4;
        REQUIRE(machine.result.stackSize == 4);
        REQUIRE(machine.stack[0] == (projectionY ? 0 : 16384));
        REQUIRE(machine.stack[1] == (projectionY ? 16384 : 0));
        REQUIRE(machine.stack[2] == (freedomY ? 0 : 16384));
        REQUIRE(machine.stack[3] == (freedomY ? 16384 : 0));
        REQUIRE(memory.state.geometry.dualProjection.x == machine.stack[0]);
        REQUIRE(memory.state.geometry.dualProjection.y == machine.stack[1]);
      }
      REQUIRE(machine.run({0x02, 0x0e, 0x0d}, memory.state) == Error::None);
      REQUIRE(machine.stack[0] == 0);
      REQUIRE(machine.stack[1] == 16384);
      Machine small(1);
      REQUIRE(small.run({0x0c}, memory.state) == Error::StackOverflow);
    }

    TEST_CASE("font VM resets vectors reference points zones and loop at glyph entry", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      REQUIRE(memory.reset() == Error::None);
      const Program change{0, 0xb0, 4, 0x10, 0xb0, 5, 0x11, 0xb0, 6, 0x12,
                           0xb0, 0, 0x16, 0xb0, 7, 0x17};
      REQUIRE(machine.run(change, memory.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(memory.state.geometry.reference[0] == 4);
      REQUIRE(memory.state.geometry.reference[1] == 5);
      REQUIRE(memory.state.geometry.reference[2] == 6);
      REQUIRE(memory.state.geometry.zone[0] == 0);
      REQUIRE(memory.state.geometry.zone[1] == 0);
      REQUIRE(memory.state.geometry.zone[2] == 0);
      REQUIRE(memory.state.geometry.loop == 7);
      REQUIRE(machine.run({}, memory.state) == Error::None);
      for (uint32_t i = 0; i < 3; ++i) {
        REQUIRE(memory.state.geometry.reference[i] == 0);
        REQUIRE(memory.state.geometry.zone[i] == 1);
      }
      REQUIRE(memory.state.geometry.loop == 1);
      REQUIRE(memory.state.geometry.freedom.x == 16384);
      REQUIRE(machine.run(change, memory.state) == Error::None);
      REQUIRE(machine.run({0xb0, 0, 0x13, 0xb0, 0, 0x14, 0xb0, 0, 0x15}, memory.state) == Error::None);
      for (const auto zone : memory.state.geometry.zone) REQUIRE(zone == 0);
      REQUIRE(machine.run({}, memory.state) == Error::None);
      REQUIRE(memory.state.geometry.loop == 1);
    }

    TEST_CASE("font VM instruction control selects defaults without erasing persistent writes", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      REQUIRE(memory.reset() == Error::None);
      const Program prep{0x7c, 0xb0, 11, 0x1d, 0xb1, 0, 42, 0x42,
                         0xb1, 1, 99, 0x44, 0xb1, 2, 2, 0x8e};
      REQUIRE(machine.run(prep, memory.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(memory.state.parameters.cvtCutIn == 11);
      REQUIRE(memory.state.prepared.cvtCutIn == 68);
      REQUIRE(memory.state.prepared.round == RoundMode::Grid);
      REQUIRE(memory.state.preparedInstructionControl == 2);
      REQUIRE(machine.run({0xb0, 0, 0x43, 0xb0, 1, 0x45, 0xb0, 31, 0x68}, memory.state) == Error::None);
      REQUIRE(machine.stack[0] == 42);
      REQUIRE(machine.stack[1] == 99);
      REQUIRE(machine.stack[2] == 0);
      REQUIRE(machine.run({0x7c, 0xb1, 0, 2, 0x8e}, memory.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(memory.state.prepared.round == RoundMode::Up);
      REQUIRE(memory.state.preparedInstructionControl == 0);
    }

    TEST_CASE("font VM reports inhibited glyphs without executing or decoding their bytecode", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      REQUIRE(memory.reset() == Error::None);
      REQUIRE(machine.run({0xb1, 1, 1, 0x8e, 0xb1, 2, 2, 0x8e},
                          memory.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(memory.state.preparedInstructionControl == 3);
      REQUIRE(machine.run({0x40}, memory.state) == Error::None); // Truncated PUSH, deliberately not decoded.
      REQUIRE(machine.result.glyphInhibited);
      REQUIRE(machine.result.instructions == 0);
      REQUIRE(machine.result.work == 0);
      REQUIRE(machine.result.stackSize == 0);
      REQUIRE(memory.state.ready);
      REQUIRE(machine.run({0xb1, 0, 1, 0x8e}, memory.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(machine.run({0x90}, memory.state) == Error::UnsupportedInstruction);
      REQUIRE_FALSE(machine.result.glyphInhibited);
    }

    TEST_CASE("font VM keeps glyph instruction control local including inside calls", "[font][fontvm]") {
      Functions functions;
      Machine machine;
      const Program font{0xb0, 0, 0x2c, 0xb1, 1, 1, 0x8e, 0xb1, 2, 2, 0x8e,
                         0xb1, 4, 3, 0x8e, 0x2d};
      REQUIRE(machine.run(font, functions.state, MaxWork, Phase::Font) == Error::None);
      REQUIRE(machine.run({0xb0, 0, 0x2b, 0xb0, 42}, functions.state) == Error::None);
      REQUIRE(machine.stack[0] == 42);
      REQUIRE(functions.state.instructionControl == 4);
      REQUIRE(functions.state.preparedInstructionControl == 0);
      REQUIRE(machine.run({}, functions.state) == Error::None);
      REQUIRE(functions.state.instructionControl == 0);
      REQUIRE(machine.run({0xb0, 0, 0x2b}, functions.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(functions.state.preparedInstructionControl == 7);
      REQUIRE(machine.run({0x90}, functions.state) == Error::None);
      REQUIRE(machine.result.glyphInhibited);
      functions.reset();
      REQUIRE(functions.state.preparedInstructionControl == 0);
    }

    TEST_CASE("font VM retains scan policy through preparation and resets glyph changes", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      REQUIRE(memory.reset() == Error::None);
      REQUIRE(machine.run({0xb8, 1, 255, 0x85, 0xb0, 4, 0x8d},
                          memory.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(memory.state.prepared.scanControl == 0x1ff);
      REQUIRE(memory.state.prepared.scanType == 4);
      REQUIRE(machine.run({0xb0, 0, 0x85, 0xb0, 2, 0x8d}, memory.state) == Error::None);
      REQUIRE(memory.state.parameters.scanControl == 0);
      REQUIRE(memory.state.parameters.scanType == 2);
      REQUIRE(machine.run({}, memory.state) == Error::None);
      REQUIRE(memory.state.parameters.scanControl == 0x1ff);
      REQUIRE(memory.state.parameters.scanType == 4);
      Program high;
      integer(high, 0x10001); high.push_back(0x85);
      integer(high, 0x10007); high.push_back(0x8d);
      REQUIRE(machine.run(high, memory.state) == Error::None);
      REQUIRE(memory.state.parameters.scanControl == 1);
      REQUIRE(memory.state.parameters.scanType == 7);
    }

    TEST_CASE("font VM validates selectors control operands and graphics state bounds", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      for (const int op : {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x85, 0x8d, 0x8e}) {
        REQUIRE(memory.reset() == Error::None);
        REQUIRE(machine.run({uint8_t(op)}, memory.state) == Error::StackUnderflow);
      }
      for (const int op : {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17}) {
        REQUIRE(memory.reset() == Error::None);
        REQUIRE(machine.run({0xb8, 0xff, 0xff, uint8_t(op)}, memory.state) == Error::InvalidParameter);
      }
      for (const int op : {0x13, 0x14, 0x15, 0x16}) {
        REQUIRE(memory.reset() == Error::None);
        REQUIRE(machine.run({0xb0, 2, uint8_t(op)}, memory.state) == Error::InvalidParameter);
      }
      for (const Program &program : {Program{0xb1, 1, 0, 0x8e}, Program{0xb1, 1, 4, 0x8e},
                                    Program{0xb1, 2, 1, 0x8e}, Program{0xb1, 1, 2, 0x8e},
                                    Program{0xb1, 1, 3, 0x8e}, Program{0xb0, 8, 0x8d}}) {
        REQUIRE(memory.reset() == Error::None);
        REQUIRE(machine.run(program, memory.state, MaxWork, Phase::Prep) == Error::InvalidParameter);
      }
      REQUIRE(memory.reset() == Error::None);
      REQUIRE(machine.run({0xb1, 1, 1, 0x8e}, memory.state, MaxWork, Phase::Font) == Error::InvalidParameter);
      REQUIRE(memory.reset() == Error::None);
      Program loop;
      integer(loop, MaxWork + 1); loop.push_back(0x17);
      REQUIRE(machine.run(loop, memory.state) == Error::InvalidParameter);
      REQUIRE(memory.reset() == Error::None);
      REQUIRE(machine.run({0xb0, 0, 0x17}, memory.state) == Error::None);
      REQUIRE(memory.state.geometry.loop == 0);
    }

    TEST_CASE("font VM instruction control changes individual flags and failed prep is not published", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      REQUIRE(memory.reset() == Error::None);
      REQUIRE(machine.run({0x7c, 0xb1, 1, 1, 0x8e, 0xb1, 2, 2, 0x8e, 0xb1, 0, 2, 0x8e},
                          memory.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(memory.state.preparedInstructionControl == 1);
      REQUIRE(memory.state.prepared.round == RoundMode::Up);
      REQUIRE(machine.run({0xb1, 1, 1, 0x8e, 0xb1, 2, 2, 0x8e, 0xb1, 0, 1, 0x8e},
                          memory.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(memory.state.preparedInstructionControl == 2);
      REQUIRE(machine.run({0x90}, memory.state) == Error::UnsupportedInstruction);
      REQUIRE(memory.reset() == Error::None);
      REQUIRE(machine.run({0x7c}, memory.state, MaxWork, Phase::Prep) == Error::None);
      REQUIRE(machine.run({0xb1, 1, 1, 0x8e, 0x90}, memory.state, MaxWork, Phase::Prep) ==
              Error::UnsupportedInstruction);
      REQUIRE(memory.state.preparedInstructionControl == 0);
      REQUIRE(memory.state.prepared.round == RoundMode::Up);
      REQUIRE_FALSE(memory.state.ready);
    }

    TEST_CASE("font VM inhibited execution still validates caller workspace", "[font][fontvm]") {
      Memory memory;
      Machine machine;
      REQUIRE(memory.reset() == Error::None);
      REQUIRE(machine.run({0xb1, 1, 1, 0x8e}, memory.state, MaxWork, Phase::Prep) == Error::None);
      const uint8_t program[]{0x90};
      int32_t stack[1]{};
      const auto result = execute({program, 1}, {stack, 1}, {}, memory.state, MaxWork);
      REQUIRE(result.error == Error::InvalidWorkspace);
      REQUIRE_FALSE(result.glyphInhibited);
      REQUIRE_FALSE(memory.state.ready);
    }

    TEST_CASE("font VM shares geometry through calls and accepts maximum bounded loop", "[font][fontvm]") {
      Functions functions;
      Machine machine;
      const Program font{0xb0, 0, 0x2c, 0x00, 0xb0, 0, 0x16, 0xb0, 3, 0x17, 0x2d};
      REQUIRE(machine.run(font, functions.state, MaxWork, Phase::Font) == Error::None);
      REQUIRE(machine.run({0xb0, 0, 0x2b, 0x0c, 0x0d}, functions.state) == Error::None);
      REQUIRE(machine.stack[0] == 0);
      REQUIRE(machine.stack[1] == 16384);
      REQUIRE(machine.stack[2] == 0);
      REQUIRE(machine.stack[3] == 16384);
      REQUIRE(functions.state.geometry.zone[2] == 0);
      REQUIRE(functions.state.geometry.loop == 3);
      Program maximum;
      integer(maximum, MaxWork); maximum.push_back(0x17);
      integer(maximum, INT32_MAX); maximum.push_back(0x10);
      REQUIRE(machine.run(maximum, functions.state) == Error::None);
      REQUIRE(functions.state.geometry.loop == MaxWork);
      REQUIRE(functions.state.geometry.reference[0] == uint32_t(INT32_MAX));
    }

TEST_CASE("font VM GETINFO reports only version and no optional capabilities", "[font][fontvm]") {
  Memory memory;
  Machine machine;
  REQUIRE(memory.reset() == Error::None);
  REQUIRE(machine.run({0xb0, 1, 0x88, 0xb0, 0xff, 0x88, 0xb0, 0, 0x88}, memory.state) == Error::None);
  REQUIRE(machine.result.stackSize == 3);
  REQUIRE(machine.stack[0] == int32_t(InterpreterVersion));
  REQUIRE(machine.stack[1] == int32_t(InterpreterVersion));
  REQUIRE(machine.stack[2] == 0);
  REQUIRE(machine.run({0x88}, memory.state) == Error::StackUnderflow);
}

namespace {
struct Zones {
  Point twilightOriginal[3]{}, twilightCurrent[3]{};
  uint8_t twilightTouched[3]{};
  Point glyphOriginal[3]{}, glyphCurrent[3]{};
  uint8_t glyphTouched[3]{};
  Memory memory;
  Zones() {
    memory.state.twilight = {{twilightOriginal, 3}, {twilightCurrent, 3}, {twilightTouched, 3}};
    memory.state.glyph = {{glyphOriginal, 3}, {glyphCurrent, 3}, {glyphTouched, 3}};
    REQUIRE(memory.reset() == Error::None);
    load(0, {100, 70});
    load(1, {770, -20});
    load(2, {-90, 10});
  }
  kstd::error_code restore() {
    load(0, {100, 70});
    load(1, {770, -20});
    load(2, {-90, 10});
    return memory.reset();
  }
  void load(int index, Point point) {
    glyphOriginal[index] = glyphCurrent[index] = point;
    glyphTouched[index] = 0;
  }
};
} // namespace

TEST_CASE("font VM MDAP rounds or touches points along the freedom vector", "[font][fontvm][points]") {
  Zones zones;
  Machine machine;
  REQUIRE(machine.run({0xb0, 0, 0x2f, 0xb0, 2, 0x2f}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[0].x == 128);
  REQUIRE(zones.glyphCurrent[0].y == 70);
  REQUIRE(zones.glyphTouched[0] == 1);
  REQUIRE(zones.glyphCurrent[2].x == -64); // Rounding is sign-symmetric.
  REQUIRE(zones.glyphOriginal[0].x == 100);
  REQUIRE(zones.memory.state.geometry.reference[0] == 2);
  REQUIRE(zones.memory.state.geometry.reference[1] == 2);
  REQUIRE(machine.run({0xb0, 1, 0x2e}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[1].x == 770);
  REQUIRE(zones.glyphTouched[1] == 1);
  REQUIRE(machine.run({0x00, 0xb0, 0, 0x2f, 0xb0, 1, 0x2f}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[0].y == 64);
  REQUIRE(zones.glyphCurrent[1].y == 0);
  REQUIRE(zones.glyphCurrent[1].x == 770);
  REQUIRE(zones.glyphTouched[0] == 3);
  REQUIRE(zones.glyphTouched[1] == 3);
}

TEST_CASE("font VM MIAP uses CVT distances, cut-in and twilight placement", "[font][fontvm][points]") {
  Zones zones;
  Machine machine;
  REQUIRE(machine.run({0xb0, 0, 0x16, 0xb0, 2, 0xb0, 0, 0x3e}, zones.memory.state) == Error::None);
  REQUIRE(zones.twilightOriginal[2].x == 768);
  REQUIRE(zones.twilightCurrent[2].x == 768);
  REQUIRE(zones.twilightTouched[2] == 1);
  REQUIRE(zones.memory.state.geometry.reference[0] == 2);
  REQUIRE(zones.memory.state.geometry.reference[1] == 2);
  // Zone selection is per run: the next glyph run returns to the glyph zone.
  REQUIRE(machine.run({0xb0, 1, 0xb0, 0, 0x3f}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[1].x == 768); // Within cut-in, so the CVT value is used.
  REQUIRE(machine.run({0xb0, 0, 0xb0, 0, 0x3f}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[0].x == 128); // Outside cut-in, so rounded current distance.
  REQUIRE(machine.run({0xb0, 2, 0xb0, 1, 0x3e}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[2].x == -256); // Unrounded negative CVT distance.
}

TEST_CASE("font VM measures original and grid-fitted points and positions with SCFS", "[font][fontvm][points]") {
  Zones zones;
  Machine machine;
  REQUIRE(machine.run({0xb0, 0, 0x2f}, zones.memory.state) == Error::None);
  REQUIRE(machine.run({0xb0, 0, 0x46, 0xb0, 0, 0x47, 0xb0, 1, 0xb0, 0, 0x49,
                       0xb0, 1, 0xb0, 0, 0x4a}, zones.memory.state) == Error::None);
  REQUIRE(machine.result.stackSize == 4);
  REQUIRE(machine.stack[0] == 128);
  REQUIRE(machine.stack[1] == 100);
  REQUIRE(machine.stack[2] == 770 - 128); // Lower operand in zone 0 minus top operand in zone 1.
  REQUIRE(machine.stack[3] == 770 - 100);
  REQUIRE(machine.run({0xb1, 2, 200, 0x48}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[2].x == 200);
  REQUIRE(zones.glyphOriginal[2].x == -90);
  REQUIRE(zones.glyphTouched[2] == 1);
  // GC on the y axis uses the same selected vectors.
  REQUIRE(machine.run({0x00, 0xb0, 1, 0x46, 0xb0, 1, 0x47}, zones.memory.state) == Error::None);
  REQUIRE(machine.stack[0] == -20);
  REQUIRE(machine.stack[1] == -20);
}

TEST_CASE("font VM moves diagonally constrained points without amplifying", "[font][fontvm][points]") {
  Zones zones;
  Machine machine;
  // Projection on y, freedom on x: near-perpendicular vectors are treated as parallel.
  REQUIRE(machine.run({0x02, 0x05, 0xb0, 0, 0x2f}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[0].x == 100 - 6);
  REQUIRE(zones.glyphCurrent[0].y == 70);
  REQUIRE(zones.glyphTouched[0] == 1);
}

TEST_CASE("font VM rejects invalid points zones CVT entries and incomplete operands", "[font][fontvm][points]") {
  Zones zones;
  Machine machine;
  for (const int op : {0x2e, 0x2f, 0x3e, 0x3f, 0x46, 0x47, 0x48, 0x49, 0x4a}) {
    REQUIRE(zones.memory.reset() == Error::None);
    REQUIRE(machine.run({uint8_t(op)}, zones.memory.state) == Error::StackUnderflow);
  }
  for (const Program &program : {Program{0xb0, 3, 0x2f}, Program{0xb8, 0xff, 0xff, 0x2e},
                                Program{0xb0, 3, 0xb0, 0, 0x3e}, Program{0xb0, 3, 0x46},
                                Program{0xb0, 3, 0xb0, 0, 0x48},
                                Program{0xb0, 3, 0xb0, 0, 0x49}, Program{0xb0, 0, 0xb0, 3, 0x4a}}) {
    REQUIRE(zones.memory.reset() == Error::None);
    REQUIRE(machine.run(program, zones.memory.state) == Error::InvalidPoint);
  }
  REQUIRE(zones.memory.reset() == Error::None);
  REQUIRE(machine.run({0xb0, 0, 0xb0, 3, 0x3e}, zones.memory.state) == Error::InvalidCvtIndex);
  REQUIRE(zones.memory.reset() == Error::None);
  REQUIRE(machine.run({0xb0, 0, 0x2f}, zones.memory.state) == Error::None);
  zones.memory.state.glyph.touched = {zones.glyphTouched, 2};
  REQUIRE(machine.run({}, zones.memory.state) == Error::InvalidState);
  zones.memory.state.glyph.touched = {zones.glyphTouched, 3};
  REQUIRE(zones.memory.reset() == Error::None);
  zones.twilightCurrent[1] = {5, 5};
  zones.twilightTouched[1] = 3;
  REQUIRE(zones.memory.reset() == Error::None);
  REQUIRE(zones.twilightCurrent[1].x == 0);
  REQUIRE(zones.twilightTouched[1] == 0);
}

TEST_CASE("font VM MDRP and MSIRP position points relative to rp0", "[font][fontvm][points]") {
  Zones zones;
  Machine machine;
  REQUIRE(machine.run({0xb0, 1, 0xd4}, zones.memory.state) == Error::None); // MDRP[rp0,round]
  REQUIRE(zones.glyphCurrent[1].x == 740); // 670 rounds down to 640 from rp0 at 100.
  REQUIRE(zones.glyphTouched[1] == 1);
  REQUIRE(zones.memory.state.geometry.reference[0] == 1);
  REQUIRE(zones.memory.state.geometry.reference[1] == 0);
  REQUIRE(zones.memory.state.geometry.reference[2] == 1);
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb0, 0, 0xc8}, zones.memory.state) == Error::None); // MDRP[min]
  REQUIRE(zones.glyphCurrent[0].x == 164);
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb0, 1, 0xc0}, zones.memory.state) == Error::None); // Unrounded.
  REQUIRE(zones.glyphCurrent[1].x == 770);
  REQUIRE(zones.memory.state.geometry.reference[0] == 0);
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb0, 1, 0xb0, 100, 0x3b}, zones.memory.state) == Error::None); // MSIRP[1]
  REQUIRE(zones.glyphCurrent[1].x == 200);
  REQUIRE(zones.memory.state.geometry.reference[0] == 1);
  REQUIRE(zones.memory.state.geometry.reference[2] == 1);
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb0, 0, 0x14, 0xb0, 2, 0xb0, 64, 0x3a}, zones.memory.state) == Error::None);
  REQUIRE(zones.twilightOriginal[2].x == 164); // Twilight placement relative to rp0.
  REQUIRE(zones.twilightCurrent[2].x == 164);
}

TEST_CASE("font VM MIRP applies cut-in, auto-flip, minimum distance and twilight placement", "[font][fontvm][points]") {
  Zones zones;
  Machine machine;
  REQUIRE(machine.run({0xb0, 0, 0xb0, 0, 0x3e}, zones.memory.state) == Error::None); // Unused; sets rp.
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb0, 1, 0xb0, 0, 0xe4}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[1].x == 740); // CVT 768 is outside cut-in, so 670 rounds to 640.
  REQUIRE(zones.restore() == Error::None);
  zones.load(1, {800, 0});
  REQUIRE(machine.run({0xb0, 1, 0xb0, 0, 0xf4}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[1].x == 868); // Exactly at cut-in, so the CVT value is kept.
  REQUIRE(zones.memory.state.geometry.reference[0] == 1);
  REQUIRE(zones.memory.state.geometry.reference[1] == 0);
  REQUIRE(zones.memory.state.geometry.reference[2] == 1);
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb0, 2, 0xb0, 0, 0xe0}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[2].x == -668); // Auto-flip makes the positive CVT value negative.
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb0, 0, 0xb8, 0xff, 0xff, 0xe8}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[0].x == 164); // Entry -1 means zero distance, raised to the minimum.
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb0, 0, 0x14, 0xb0, 2, 0xb0, 0, 0xe0}, zones.memory.state) == Error::None);
  REQUIRE(zones.twilightOriginal[2].x == 868);
  REQUIRE(zones.twilightOriginal[2].y == 70);
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb0, 1, 0xb0, 3, 0xe0}, zones.memory.state) == Error::InvalidCvtIndex);
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb0, 3, 0xb0, 0, 0xe0}, zones.memory.state) == Error::InvalidPoint);
  for (const Program &program : {Program{0xc0}, Program{0xe0}, Program{0xb0, 0, 0xe0}, Program{0x3a}}) {
    REQUIRE(zones.restore() == Error::None);
    REQUIRE(machine.run(program, zones.memory.state) == Error::StackUnderflow);
  }
}

TEST_CASE("font VM IP and SHP follow reference points", "[font][fontvm][points]") {
  Zones zones;
  Machine machine;
  zones.glyphCurrent[1].x = 1440; // Reference span doubles from 670 to 1340.
  REQUIRE(machine.run({0xb0, 0, 0x11, 0xb0, 1, 0x12, 0xb0, 2, 0x39}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[2].x == -280);
  REQUIRE(zones.glyphTouched[2] == 1);
  REQUIRE(zones.restore() == Error::None);
  zones.glyphCurrent[1].x = 830; // Displacement of 60 from its original position.
  REQUIRE(machine.run({0xb0, 1, 0x12, 0xb0, 2, 0x32}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[2].x == -30);
  REQUIRE(zones.glyphTouched[2] == 1);
  REQUIRE(zones.restore() == Error::None);
  zones.glyphCurrent[1].x = 830;
  REQUIRE(machine.run({0xb0, 1, 0x11, 0xb0, 2, 0x33}, zones.memory.state) == Error::None); // rp1 variant.
  REQUIRE(zones.glyphCurrent[2].x == -30);
  for (const Program &program : {Program{0xb0, 3, 0x39}, Program{0xb0, 0, 0x39, 0x39}, Program{0xb0, 3, 0x32}}) {
    REQUIRE(zones.restore() == Error::None);
    const auto error = machine.run(program, zones.memory.state);
    REQUIRE((error == Error::InvalidPoint || error == Error::StackUnderflow));
  }
}

namespace {
bool near(int32_t actual, int32_t expected, int32_t tolerance = 2) {
  return actual >= expected - tolerance && actual <= expected + tolerance;
}
} // namespace

TEST_CASE("font VM sets vectors from points and stack values", "[font][fontvm][points]") {
  Zones zones;
  Machine machine;
  auto &geometry = zones.memory.state.geometry;
  REQUIRE(machine.run({0xb1, 0, 1, 0x06}, zones.memory.state) == Error::None); // zp1[0] - zp2[1]
  REQUIRE(near(geometry.projection.x, -16240));
  REQUIRE(near(geometry.projection.y, 2181));
  REQUIRE(geometry.dualProjection.x == geometry.projection.x);
  REQUIRE(machine.run({0xb1, 0, 1, 0x07}, zones.memory.state) == Error::None); // Perpendicular.
  REQUIRE(near(geometry.projection.x, -2181));
  REQUIRE(near(geometry.projection.y, -16240));
  REQUIRE(machine.run({0xb1, 0, 1, 0x08}, zones.memory.state) == Error::None);
  REQUIRE(near(geometry.freedom.x, -16240));
  REQUIRE(machine.run({0xb1, 1, 1, 0x06}, zones.memory.state) == Error::None); // Same point: x axis.
  REQUIRE(geometry.projection.x == 16384);
  REQUIRE(geometry.projection.y == 0);
  zones.glyphCurrent[0].x += 100; // Current and original differ: SDPVTL keeps two vectors.
  REQUIRE(machine.run({0xb1, 0, 1, 0x86}, zones.memory.state) == Error::None);
  REQUIRE(near(geometry.dualProjection.x, -16240));
  REQUIRE(near(geometry.projection.x, -16183, 3));
  Program program{0xb9, 0, 0, 0x40, 0, 0x0a};
  REQUIRE(machine.run(program, zones.memory.state) == Error::None);
  REQUIRE(geometry.projection.x == 0);
  REQUIRE(geometry.projection.y == 16384);
  REQUIRE(geometry.dualProjection.y == 16384);
  Program freedom{0xb9, 0, 3, 0, 3, 0x0b}; // (3, 3) normalizes to 45 degrees.
  REQUIRE(machine.run(freedom, zones.memory.state) == Error::None);
  REQUIRE(near(geometry.freedom.x, 11585));
  REQUIRE(near(geometry.freedom.y, 11585));
  REQUIRE(machine.run({0xb0, 0, 0x0b}, zones.memory.state) == Error::StackUnderflow);
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb1, 0, 3, 0x06}, zones.memory.state) == Error::InvalidPoint);
}

TEST_CASE("font VM SROUND and S45ROUND configure super rounding", "[font][fontvm]") {
  Memory memory;
  Machine machine;
  REQUIRE(memory.reset() == Error::None);
  const auto round = [&](Program setup, int32_t value) {
    Program program = setup;
    word(program, value);
    program.push_back(0x68);
    REQUIRE(machine.run(program, memory.state) == Error::None);
    return machine.stack[0];
  };
  REQUIRE(round({0xb0, 0x48, 0x76}, 33) == 64);
  REQUIRE(round({0xb0, 0x00, 0x76}, 33) == 64);
  REQUIRE(round({0xb0, 0x00, 0x76}, 32) == 32);
  REQUIRE(round({0xb0, 0x00, 0x76}, -33) == -64);
  REQUIRE(round({0xb0, 0x00, 0x76}, 0) == 0);
  REQUIRE(round({0xb0, 0x58, 0x76}, 20) == 16); // Phase of a quarter period.
  REQUIRE(round({0xb0, 0x58, 0x76}, 50) == 80);
  REQUIRE(round({0xb0, 0x58, 0x76}, -20) == -16);
  REQUIRE(round({0xb0, 0x48, 0x77}, 50) == 45);
  REQUIRE(round({0xb0, 0x48, 0x77}, 68) == 90);
  REQUIRE(round({0xb0, 0x48, 0x77}, 67) == 45);
  REQUIRE(machine.run({0x76}, memory.state) == Error::StackUnderflow);
}

TEST_CASE("font VM UTP SHC SHZ ISECT and ALIGNPTS", "[font][fontvm][points]") {
  Zones zones;
  Machine machine;
  uint32_t ends[2]{1, 2};
  zones.memory.state.glyphContours = {ends, 2};
  REQUIRE(machine.run({0xb0, 0, 0x2e, 0xb0, 0, 0x29}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphTouched[0] == 0);
  REQUIRE(machine.run({0xb0, 0, 0x2e, 0x00, 0xb0, 0, 0x29}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphTouched[0] == 1); // y-axis UTP leaves x touch alone.

  REQUIRE(zones.restore() == Error::None);
  zones.glyphCurrent[2].x = -30; // Displacement of 60 for point 2.
  REQUIRE(machine.run({0xb0, 2, 0x12, 0xb0, 0, 0x34}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[0].x == 160);
  REQUIRE(zones.glyphCurrent[1].x == 830);
  REQUIRE(zones.glyphTouched[0] == 1);
  REQUIRE(zones.glyphTouched[1] == 1);
  REQUIRE(zones.restore() == Error::None);
  zones.glyphCurrent[2].x = -30;
  REQUIRE(machine.run({0xb0, 2, 0x11, 0xb0, 1, 0x35}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[2].x == -30); // The reference point never moves itself.
  REQUIRE(machine.run({0xb0, 2, 0x11, 0xb0, 2, 0x35}, zones.memory.state) == Error::InvalidPoint);
  REQUIRE(zones.restore() == Error::None);
  zones.glyphCurrent[2].x = -30;
  REQUIRE(machine.run({0xb0, 2, 0x12, 0xb0, 1, 0x36}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[0].x == 160);
  REQUIRE(zones.glyphCurrent[1].x == 830);
  REQUIRE(zones.glyphCurrent[2].x == -30);
  REQUIRE(zones.glyphTouched[0] == 0); // SHZ does not touch.
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb0, 2, 0x36}, zones.memory.state) == Error::InvalidParameter);

  REQUIRE(zones.restore() == Error::None);
  zones.glyphCurrent[1] = {0, 0};
  zones.glyphCurrent[2] = {128, 0};
  zones.twilightCurrent[0] = {64, -64};
  zones.twilightCurrent[1] = {64, 64};
  REQUIRE(machine.run({0xb0, 0, 0x13, 0xb4, 0, 1, 2, 0, 1, 0x0f}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[0].x == 64);
  REQUIRE(zones.glyphCurrent[0].y == 0);
  REQUIRE(zones.glyphTouched[0] == 3);
  zones.twilightCurrent[0] = {0, 64}; // Parallel lines use the average of the endpoints.
  zones.twilightCurrent[1] = {128, 64};
  REQUIRE(machine.run({0xb0, 0, 0x13, 0xb4, 0, 1, 2, 0, 1, 0x0f}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[0].x == 64);
  REQUIRE(zones.glyphCurrent[0].y == 32);
  REQUIRE(machine.run({0xb0, 0, 0x13, 0xb4, 0, 1, 2, 0, 3, 0x0f}, zones.memory.state) == Error::InvalidPoint);

  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb1, 0, 1, 0x27}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[0].x == 435);
  REQUIRE(zones.glyphCurrent[1].x == 435);
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb0, 0, 0x27}, zones.memory.state) == Error::StackUnderflow);
}

TEST_CASE("font VM DELTAP DELTAC and FLIP operations", "[font][fontvm][points]") {
  Zones zones;
  Machine machine;
  REQUIRE(machine.run({0xb2, 0x7c, 0, 1, 0x5d}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[0].x == 140); // (0xc - 8 + 1) * 8 at ppem 16.
  REQUIRE(zones.glyphTouched[0] == 1);
  REQUIRE(machine.run({0xb2, 0x8c, 1, 1, 0x5d}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[1].x == 770); // Wrong ppem: no movement.
  REQUIRE(machine.run({0xb2, 0x7c, 0, 1, 0x73}, zones.memory.state) == Error::None);
  REQUIRE(zones.memory.cvt[0] == 808);
  REQUIRE(machine.run({0xb2, 0x7c, 3, 1, 0x73}, zones.memory.state) == Error::InvalidCvtIndex);
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb2, 0x7c, 3, 1, 0x5d}, zones.memory.state) == Error::InvalidPoint);
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb1, 0x7c, 1, 0x5d}, zones.memory.state) == Error::StackUnderflow);
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(zones.memory.reset(25) == Error::None);
  REQUIRE(machine.run({0xb2, 0x00, 0, 1, 0x71}, zones.memory.state) == Error::None);
  REQUIRE(zones.glyphCurrent[0].x == 36); // Base 9 plus 16: size 25, step -8 * 8.

  uint8_t onCurve[3]{1, 1, 0};
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb0, 0, 0x80}, zones.memory.state) == Error::InvalidState);
  REQUIRE(zones.restore() == Error::None);
  zones.memory.state.glyph.onCurve = {onCurve, 3};
  REQUIRE(machine.run({0xb0, 2, 0x17, 0xb1, 0, 2, 0x80}, zones.memory.state) == Error::None);
  REQUIRE((onCurve[0] == 0 && onCurve[1] == 1 && onCurve[2] == 1));
  REQUIRE(machine.run({0xb1, 0, 1, 0x82}, zones.memory.state) == Error::None);
  REQUIRE((onCurve[0] == 0 && onCurve[1] == 0 && onCurve[2] == 1));
  REQUIRE(machine.run({0xb1, 1, 2, 0x81}, zones.memory.state) == Error::None);
  REQUIRE((onCurve[0] == 0 && onCurve[1] == 1 && onCurve[2] == 1));
  REQUIRE(zones.restore() == Error::None);
  REQUIRE(machine.run({0xb1, 0, 3, 0x81}, zones.memory.state) == Error::InvalidPoint);
}

namespace {
struct InstructionDefs {
  Function functions[2]{};
  InstructionDef definitions[2]{};
  State state{};
  InstructionDefs() {
    state.functions = {functions, 2};
    state.instructionDefs = {definitions, 2};
    reset();
  }
  void reset() { REQUIRE(initialize(state, {}, 16, 1000) == Error::None); }
};
} // namespace

TEST_CASE("font VM runs IDEF bodies for undefined opcodes", "[font][fontvm]") {
  using namespace kernel::font::vm::opcode;
  InstructionDefs defs;
  Machine machine;
  // Opcode 0x28 adds 10 to the top of the stack; opcode 0x4f calls function 0 through it.
  const Program font{0xb0, 0x28, IDEF, 0xb0, 10, ADD, ENDF,
                     0xb0, 0, FDEF, 0x28, ENDF,
                     0xb0, 0x4f, IDEF, 0xb0, 0, CALL, ENDF};
  REQUIRE(machine.run({0xb0, 2, 0x28}, defs.state) == Error::UnsupportedInstruction);
  defs.reset();
  REQUIRE(machine.run(font, defs.state, MaxWork, Phase::Font) == Error::None);
  REQUIRE(machine.result.stackSize == 0);
  REQUIRE(defs.definitions[0].defined);
  REQUIRE(defs.definitions[0].opcode == 0x28);
  REQUIRE(machine.run({0xb0, 2, 0x28}, defs.state) == Error::None);
  REQUIRE(machine.result.stackSize == 1);
  REQUIRE(machine.stack[0] == 12);
  REQUIRE(machine.run({0xb0, 2, 0x4f}, defs.state) == Error::None);
  REQUIRE(machine.stack[0] == 12);
  defs.reset();
  REQUIRE(machine.run(font, defs.state, MaxWork, Phase::Font) == Error::None);
  REQUIRE(machine.run({0x28}, defs.state) == Error::StackUnderflow);
  defs.reset();
  REQUIRE(machine.run(font, defs.state, MaxWork, Phase::Font) == Error::None);
  REQUIRE(machine.run({0xb0, 2, 0x7b}, defs.state) == Error::UnsupportedInstruction);
}

TEST_CASE("font VM keeps built-in opcodes and validates IDEF operands", "[font][fontvm]") {
  using namespace kernel::font::vm::opcode;
  InstructionDefs defs;
  Machine machine;
  const Program overrideBuiltin{0xb0, ADD, IDEF, 0xb0, 99, ENDF, 0xb1, 1, 2, ADD};
  REQUIRE(machine.run(overrideBuiltin, defs.state, MaxWork, Phase::Font) == Error::None);
  REQUIRE(machine.stack[0] == 3);
  defs.reset();
  const Program redefine{0xb0, 0x28, IDEF, 0xb0, 1, ENDF, 0xb0, 0x28, IDEF, 0xb0, 2, ENDF};
  REQUIRE(machine.run(redefine, defs.state, MaxWork, Phase::Font) == Error::None);
  REQUIRE(defs.definitions[0].defined);
  REQUIRE_FALSE(defs.definitions[1].defined);
  REQUIRE(machine.run({0x28}, defs.state) == Error::None);
  REQUIRE(machine.stack[0] == 2);
  defs.reset();
  const Program full{0xb0, 0x28, IDEF, ENDF, 0xb0, 0x29, IDEF, ENDF, 0xb0, 0x2f, IDEF, ENDF};
  REQUIRE(machine.run(full, defs.state, MaxWork, Phase::Font) == Error::InvalidFunction);
  defs.reset();
  REQUIRE(machine.run({0xb0, 0x28, IDEF, ENDF}, defs.state) == Error::DefinitionInGlyph);
  defs.reset();
  REQUIRE(machine.run({0xb0, 0x28, IDEF}, defs.state, MaxWork, Phase::Font) == Error::InvalidDefinition);
  defs.reset();
  REQUIRE(machine.run({IDEF, ENDF}, defs.state, MaxWork, Phase::Font) == Error::StackUnderflow);
  defs.reset();
  REQUIRE(machine.run({0xb8, 0x01, 0x00, IDEF, ENDF}, defs.state, MaxWork, Phase::Font) == Error::InvalidParameter);
  defs.reset();
  REQUIRE(machine.run({0xb0, 0xff, IDEF, ENDF}, defs.state, MaxWork, Phase::Font) == Error::None);
}
