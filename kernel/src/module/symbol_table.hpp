
#pragma once

#include "symbol_entry.hpp"
#include <kernel/kerror.hpp>
#include <kernel/adt/avl_tree.hpp>
#include <expected.hpp>

namespace kernel::driver {
class SymbolTable {
  adt::AvlTree<SymbolEntry, &SymbolEntry::node> symbols;

  public:
  SymbolTable() = default;
  ~SymbolTable();

  SymbolTable(const SymbolTable &) = delete;
  SymbolTable &operator=(const SymbolTable &) = delete;

  KernelError Register(kstd::string_view name, uintptr_t address);
  kstd::expected<uintptr_t, KernelError> Resolve(kstd::string_view name) const;
};


}// namespace kernel::driver
