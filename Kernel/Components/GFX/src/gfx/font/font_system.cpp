#include "font_system.hpp"

#include <atomic.hpp>
#include <mutex.hpp>

namespace kernel::font {
namespace {
kstd::atomic<FontSystem *> g_system{nullptr};
Mutex g_createLock;
} // namespace

FontSystem *font_system_if_ready() { return g_system.load(kstd::memory_order_acquire); }

FontSystem *font_system() {
  if (auto *ready = font_system_if_ready()) return ready;
  kstd::unique_lock lock(g_createLock);
  if (auto *ready = font_system_if_ready()) return ready;
  auto *created = new FontSystem;
  if (created) g_system.store(created, kstd::memory_order_release);
  return created;
}

} // namespace kernel::font
