#pragma once

#include <stddef.h>
#include <stdint.h>
#include <undos/abi.h>

namespace kernel::process_startup {
constexpr size_t PATH_CAPACITY = 256;
constexpr size_t ARGUMENT_CAPACITY = 1024;
constexpr size_t MAX_ARGUMENTS = 32;

// No shell expansion or escapes: backslashes are literal. Double quotes group
// whitespace, are removed, and may concatenate with unquoted text. "" is an
// empty argument. Unclosed quotes, embedded NULs and overflow are errors.
struct Arguments {
  uint32_t argc{};
  uint32_t bytes{};
  uint32_t offsets[MAX_ARGUMENTS]{};
  char text[PATH_CAPACITY + ARGUMENT_CAPACITY]{};
};

constexpr bool whitespace(char ch) {
  return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f';
}

inline bool parse(const char *path, size_t path_size, const char *command, size_t command_size,
                  Arguments &out) {
  out.argc = out.bytes = 0;
  if (!path || !path_size || path_size >= PATH_CAPACITY ||
      command_size >= ARGUMENT_CAPACITY || (!command && command_size)) return false;
  out.offsets[out.argc++] = 0;
  for (size_t i = 0; i < path_size; ++i) {
    if (!path[i]) return false;
    out.text[out.bytes++] = path[i];
  }
  out.text[out.bytes++] = '\0';
  size_t pos = 0;
  while (pos < command_size) {
    while (pos < command_size && whitespace(command[pos])) ++pos;
    if (pos == command_size) break;
    if (out.argc == MAX_ARGUMENTS) return false;
    out.offsets[out.argc++] = out.bytes;
    bool quoted = false;
    while (pos < command_size) {
      const char ch = command[pos];
      if (!ch) return false;
      if (!quoted && whitespace(ch)) break;
      ++pos;
      if (ch == '"') quoted = !quoted;
      else out.text[out.bytes++] = ch;
    }
    if (quoted) return false;
    out.text[out.bytes++] = '\0';
  }
  return true;
}

// Entry stack: [0, startup-info address, loader-private event, 0], followed by
// the public structure, argv, its terminator, empty envp, and packed strings.
constexpr size_t stack_size(const Arguments &args) {
  return (16 + sizeof(UnDOSStartupInfoV1) + (args.argc + 2) * sizeof(uint32_t) +
          args.bytes + 15) & ~size_t(15);
}

inline bool write_stack(void *storage, size_t capacity, uint32_t user_address, const Arguments &args,
                        uint32_t startup_event = UINT32_MAX) {
  const size_t size = stack_size(args);
  if (!storage || !args.argc || args.argc > MAX_ARGUMENTS || size > capacity ||
      (user_address & 15) || user_address > UINT32_MAX - size) return false;
  auto *words = static_cast<uint32_t *>(storage);
  words[0] = 0;
  words[1] = user_address + 16;
  words[2] = startup_event;
  words[3] = 0;
  auto &info = *reinterpret_cast<UnDOSStartupInfoV1 *>(words + 4);
  info = {};
  info.size = sizeof(info);
  info.version = UNDOS_APPLICATION_ABI_VERSION;
  info.argc = args.argc;
  info.argv = user_address + 16 + sizeof(info);
  info.envp = info.argv + (args.argc + 1) * 4;
  auto *argv = reinterpret_cast<uint32_t *>(&info + 1);
  const uint32_t text_address = info.envp + 4;
  for (uint32_t i = 0; i < args.argc; ++i) argv[i] = text_address + args.offsets[i];
  argv[args.argc] = 0;
  argv[args.argc + 1] = 0;
  auto *text = reinterpret_cast<char *>(argv + args.argc + 2);
  for (uint32_t i = 0; i < args.bytes; ++i) text[i] = args.text[i];
  return true;
}
} // namespace kernel::process_startup
