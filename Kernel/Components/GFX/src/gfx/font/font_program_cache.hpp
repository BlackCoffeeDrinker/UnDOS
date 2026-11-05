#pragma once

#include "vm.hpp"

#include <atomic.hpp>
#include <kernel/mutex.hpp>
#include <mutex.hpp>
#include <vector.hpp>

namespace kernel::font::glyph {

// The state a face's font program (fpgm) leaves behind when no executed
// instruction depended on ppem or the CVT: definitions and storage. The first
// realization publishes it and later ones copy it instead of rerunning fpgm.
// A size-dependent fpgm marks the cache per-size and every size runs it.
// Function bodies borrow the face's immutable program bytes. Thread-safe.
class FontProgramCache {
public:
  // Copies the cached result into a freshly initialized state of the same
  // shape. Returns false when nothing is cached or the shapes differ.
  bool restore(vm::State &state) {
    kstd::unique_lock lock(mutex_);
    if (status_ != Status::Ready || functions_.size() != state.functions.size() ||
        instructionDefs_.size() != state.instructionDefs.size() || storage_.size() != state.storage.size())
      return false;
    for (size_t i = 0; i < functions_.size(); ++i) state.functions.data()[i] = functions_.data()[i];
    for (size_t i = 0; i < instructionDefs_.size(); ++i) state.instructionDefs.data()[i] = instructionDefs_.data()[i];
    for (size_t i = 0; i < storage_.size(); ++i) {
      state.storage.data()[i] = storage_.data()[i];
      state.storageWritten.data()[i] = written_.data()[i];
    }
    return true;
  }

  // Records a successful fpgm run. A size-dependent run disables caching.
  void publish(const vm::State &state, bool ppemDependent) {
    kstd::unique_lock lock(mutex_);
    if (status_ != Status::Empty) return;
    if (ppemDependent) { status_ = Status::PerSize; return; }
    functions_.resize(state.functions.size());
    instructionDefs_.resize(state.instructionDefs.size());
    storage_.resize(state.storage.size());
    written_.resize(state.storage.size());
    if (functions_.size() != state.functions.size() || instructionDefs_.size() != state.instructionDefs.size() ||
        storage_.size() != state.storage.size() || written_.size() != state.storage.size())
      return; // Out of memory: stay empty and keep running fpgm per size.
    for (size_t i = 0; i < functions_.size(); ++i) functions_.data()[i] = state.functions.data()[i];
    for (size_t i = 0; i < instructionDefs_.size(); ++i) instructionDefs_.data()[i] = state.instructionDefs.data()[i];
    for (size_t i = 0; i < storage_.size(); ++i) {
      storage_.data()[i] = state.storage.data()[i];
      written_.data()[i] = state.storageWritten.data()[i];
    }
    status_ = Status::Ready;
  }

  void count_run() { runs_.fetch_add(1); }
  // Number of times fpgm actually executed for this face.
  uint32_t runs() const { return runs_.load(); }
  bool per_size() {
    kstd::unique_lock lock(mutex_);
    return status_ == Status::PerSize;
  }

private:
  enum class Status : uint8_t { Empty, Ready, PerSize };
  Mutex mutex_;
  Status status_{Status::Empty};
  kstd::atomic<uint32_t> runs_{0};
  kstd::vector<vm::Function> functions_;
  kstd::vector<vm::InstructionDef> instructionDefs_;
  kstd::vector<int32_t> storage_;
  kstd::vector<uint8_t> written_;
};

} // namespace kernel::font::glyph
