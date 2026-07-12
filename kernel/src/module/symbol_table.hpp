
#pragma once

#include "symbol_entry.hpp"
#include <kernel/adt/avl_tree.hpp>

namespace kernel::driver {
class SymbolTable {
  adt::AvlTree<SymbolEntry, &SymbolEntry::node> symbols;

  public:
  SymbolTable() = default;
  ~SymbolTable();

  SymbolTable(const SymbolTable &) = delete;
  SymbolTable &operator=(const SymbolTable &) = delete;

  bool Register(kstd::string_view name, uintptr_t address);
  uintptr_t Resolve(kstd::string_view name) const;
};


}// namespace kernel::driver
