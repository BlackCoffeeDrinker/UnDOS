#include <Kernel.hpp>
#include <__fwd/panic.hpp>

extern "C" UNDOS_KERNEL_API_DEF [[noreturn]] void KE_RUNTIME_Panic(const char *message) {
  HAL_PLATFORM_Panic(message, __FILE__, __LINE__);
}

namespace {

bool IsLineBreak(char c) noexcept {
  return c == '\r' || c == '\n';
}

// Formats "[sssss.uuuuuu] " (dmesg style) into `out`; returns the length.
size_t FormatTimestamp(char (&out)[24]) noexcept {
  const auto ns = static_cast<uint64_t>(KE_TIME_GetSystemTime().count());
  uint64_t seconds = ns / 1000000000ull;
  auto micros = static_cast<uint32_t>((ns / 1000ull) % 1000000ull);

  char digits[20];
  size_t n = 0;
  do {
    digits[n++] = static_cast<char>('0' + seconds % 10);
    seconds /= 10;
  } while (seconds != 0 && n < sizeof(digits));

  size_t len = 0;
  out[len++] = '[';
  for (size_t pad = n; pad < 5; ++pad) out[len++] = ' ';
  while (n > 0 && len < 12) out[len++] = digits[--n];
  out[len++] = '.';
  for (size_t d = 6; d > 0; --d) {
    out[len + d - 1] = static_cast<char>('0' + micros % 10);
    micros /= 10;
  }
  len += 6;
  out[len++] = ']';
  out[len++] = ' ';
  return len;
}

void PrintLine(const char *str, size_t len) noexcept {
  char prefix[24];
  // TODO: Dump to console - calling HAL_Debug_FirmwarePrint is WRONG
  HAL_Debug_FirmwarePrint(prefix, FormatTimestamp(prefix));
  if (len > 0) HAL_Debug_FirmwarePrint(str, len);
  HAL_Debug_FirmwarePrint("\r\n", 2);
}

}// namespace

void KE_PrintBuffer(const char *str, size_t len) noexcept {
  if (!str) return;

  while (len > 0 && IsLineBreak(str[len - 1])) --len;

  // Embedded line breaks ("\r\n", "\n\r", "\n", "\r") each start a new line.
  size_t start = 0;
  for (size_t i = 0; i < len; ++i) {
    if (!IsLineBreak(str[i])) continue;
    PrintLine(str + start, i - start);
    if (i + 1 < len && IsLineBreak(str[i + 1]) && str[i + 1] != str[i]) ++i;
    start = i + 1;
  }
  PrintLine(str + start, len - start);
}
