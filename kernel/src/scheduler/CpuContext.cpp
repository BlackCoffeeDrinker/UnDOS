#include "CpuContext.hpp"

namespace kernel::cpu {
namespace {
// Statically allocated: the BSP needs its slot before the heap exists, and an
// AP needs its own slot before it can run anything that could allocate.
PerCpu<CpuContext> g_contexts;
}// namespace

PerCpu<CpuContext> &contexts() noexcept {
  return g_contexts;
}

CpuContext &current_context() noexcept {
  return g_contexts.current();
}

uint32_t online_count() noexcept {
  uint32_t count = 0;
  for (size_t i = 0; i < PerCpu<CpuContext>::size(); ++i) {
    if (g_contexts[i].online) ++count;
  }
  return count;
}

void init_contexts() noexcept {
  for (size_t i = 0; i < PerCpu<CpuContext>::size(); ++i) {
    g_contexts[i].id = static_cast<uint32_t>(i);
  }
}

}// namespace kernel::cpu
