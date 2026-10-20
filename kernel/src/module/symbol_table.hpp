
#pragma once

#include "symbol_entry.hpp"
#include <expected.hpp>

#include <kernel/adt/avl_tree.hpp>
#include <kernel/kerror.hpp>

namespace kernel::driver {
class SymbolTable {
  adt::AvlTree<SymbolEntry, &SymbolEntry::node> symbols;

  public:
  SymbolTable() = default;
  ~SymbolTable();

  SymbolTable(const SymbolTable &) = delete;
  SymbolTable &operator=(const SymbolTable &) = delete;

  kstd::error_code Register(const kstd::string_view &name, uintptr_t address);
  [[nodiscard]] kstd::expected<uintptr_t, kstd::error_code> Resolve(kstd::string_view name) const;
};


}// namespace kernel::driver
