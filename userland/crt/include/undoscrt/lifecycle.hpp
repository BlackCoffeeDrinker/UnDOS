#pragma once

#include <stddef.h>

namespace undos::crt {
class ExitCallbacks {
  struct Entry {
    void (*function)(void *){};
    void *argument{};
    void *dso{};
    void (*plain)(){};
  };
  static constexpr size_t CAPACITY = 128;
  Entry entries[CAPACITY]{};
  size_t count{};

public:
  int add(void (*function)(void *), void *argument, void *dso) {
    if (!function || count == CAPACITY) return -1;
    entries[count++] = {function, argument, dso, nullptr};
    return 0;
  }

  int add(void (*function)()) {
    if (!function || count == CAPACITY) return -1;
    entries[count++] = {nullptr, nullptr, nullptr, function};
    return 0;
  }

  void finalize(void *dso = nullptr) {
    // Restart after each callback: callbacks may register more callbacks.
    for (size_t i = count; i > 0;) {
      auto &entry = entries[--i];
      if ((!entry.function && !entry.plain) || (dso && entry.dso != dso)) continue;
      const Entry call = entry;
      entry.function = nullptr;
      entry.plain = nullptr;
      if (call.plain) call.plain();
      else call.function(call.argument);
      i = count;
    }
    while (count && !entries[count - 1].function && !entries[count - 1].plain) --count;
  }
};
}
