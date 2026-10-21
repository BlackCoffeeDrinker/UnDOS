
#pragma once

#include <kernel/memory/address.hpp>

namespace kernel {
struct PageFaultInfo {
  VirtualAddress fault_addr;
  bool present;        // page present bit
  bool write;          // attempted write
  bool user;           // user-mode fault
  bool exec;           // attempted instruction fetch (if arch supports)
  VirtualAddress ip;   // faulting instruction pointer
};
}
