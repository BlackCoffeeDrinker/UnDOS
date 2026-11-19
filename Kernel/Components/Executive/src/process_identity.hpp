#pragma once

#include <stdint.h>

namespace kernel::process {

class IdentitySequence {
 public:
  constexpr IdentitySequence(uint32_t next_pid = 1,
                             uint64_t next_incarnation = 1) noexcept
      : next_pid_(next_pid), next_incarnation_(next_incarnation) {}

  bool assign(uint32_t &pid, uint64_t &incarnation) noexcept {
    if (!next_pid_ || !next_incarnation_) return false;
    pid = next_pid_;
    incarnation = next_incarnation_;
    next_pid_ = next_pid_ == UINT32_MAX ? 0 : next_pid_ + 1;
    next_incarnation_ =
        next_incarnation_ == UINT64_MAX ? 0 : next_incarnation_ + 1;
    return true;
  }

 private:
  uint32_t next_pid_;
  uint64_t next_incarnation_;
};

}  // namespace kernel::process
