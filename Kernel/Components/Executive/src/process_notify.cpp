
#include "process_notify.hpp"

namespace kernel {
namespace {
struct Entry {
  ProcessNotifyFn fn;
  void *ctx;
  ProcessNotifyOrder order;
};

constexpr size_t kMaxProcessNotify = 8;
// Written only during single-threaded init; dispatch reads without a lock.
Entry g_entries[kMaxProcessNotify]{};
size_t g_count = 0;
}// namespace

kstd::error_code KE_PS_RegisterNotify(ProcessNotifyFn fn, void *ctx, ProcessNotifyOrder order) noexcept {
  if (!fn) return KernelError::InvalidArgument;
  if (g_count == kMaxProcessNotify) return KernelError::ResourceLimitExceeded;
  size_t pos = 0;
  while (pos < g_count && g_entries[pos].order < order) ++pos;
  if (pos < g_count && g_entries[pos].order == order) return KernelError::InvalidState;
  for (size_t i = g_count; i > pos; --i) g_entries[i] = g_entries[i - 1];
  g_entries[pos] = {fn, ctx, order};
  ++g_count;
  return {};
}

kstd::error_code ps_notify(ProcessEvent event, KProcessObject &process) noexcept {
  const size_t count = g_count;
  if (event == ProcessEvent::Exiting) {
    for (size_t i = 0; i < count; ++i) (void) g_entries[i].fn(ProcessEvent::Exiting, process, g_entries[i].ctx);
    return {};
  }
  for (size_t i = 0; i < count; ++i) {
    if (const auto err = g_entries[i].fn(ProcessEvent::Creating, process, g_entries[i].ctx)) {
      while (i-- > 0) (void) g_entries[i].fn(ProcessEvent::Exiting, process, g_entries[i].ctx);
      return err;
    }
  }
  return {};
}
}// namespace kernel
