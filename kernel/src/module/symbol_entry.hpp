
#pragma once

#include "kernel/adt/avl_tree.hpp"

#include <static_string.hpp>

namespace kernel::driver {

struct SymbolEntry {
  kstd::static_string<64> name;
  uintptr_t address;
  adt::AvlNode<SymbolEntry> node;

  bool operator<(const SymbolEntry &other) const { return name < other.name; }
  bool operator==(const SymbolEntry &other) const { return name == other.name; }

  bool operator<(kstd::string_view other_name) const { return kstd::string_view(name) < other_name; }
  bool operator==(kstd::string_view other_name) const { return kstd::string_view(name) == other_name; }

  friend bool operator<(kstd::string_view lhs, const SymbolEntry &rhs) { return lhs < kstd::string_view(rhs.name); }
  friend bool operator==(kstd::string_view lhs, const SymbolEntry &rhs) { return lhs == kstd::string_view(rhs.name); }
};
}// namespace kernel::driver
