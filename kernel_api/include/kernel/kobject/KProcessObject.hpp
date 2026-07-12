#pragma once

#include <kernel/__core.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/KThreadObject.hpp>
#include <kernel/kobject/ObjectType.hpp>
#include <kernel/virtual_memory.hpp>

namespace kernel {

// Process Control Block (EPROCESS-style). Owns the isolated address space and
// the thread(s) running inside it. Platform-agnostic: all HAL-specific state
// lives inside vmm::AddressSpace / KThreadObject.
struct KProcessObject : KObjectT<KProcessObject, 1, TYPE_PROCESS> {
  enum class State : uint8_t { Running, Terminated };

  vmm::AddressSpace address_space{};
  KObjectPtr<KThreadObject> main_thread;
  int32_t exit_code{0};
  State state{State::Running};
};

template<>
struct ObjectTypeOf<TYPE_PROCESS> {
  using type = KProcessObject;
};

}// namespace kernel
