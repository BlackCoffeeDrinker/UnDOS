
#pragma once

namespace kernel::memory {
struct AddressSpace {
  PhysicalAddress base;
  size_t size;
};

}// namespace kernel::memory
