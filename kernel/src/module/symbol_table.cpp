
#include "symbol_table.hpp"

#include "stdkrn.hpp"


namespace kernel::driver {

SymbolTable::~SymbolTable() {
  symbols.clear([](SymbolEntry *entry) {
    entry->~SymbolEntry();
    KE_Free(entry);
  });
}

bool SymbolTable::Register(kstd::string_view name, uintptr_t address) {
  auto entry = KE_CreateObject<SymbolEntry>();
  if (!entry) return false;

  entry->name = name;
  entry->address = address;
  symbols.insert(*entry);

  // The AVL tree now owns this memory (linked in by address); release the
  // unique_ptr so it isn't freed here out from under the tree. It is freed
  // later via KE_Free in the destructor's clear() callback above.
  entry.release();
  return true;
}

uintptr_t SymbolTable::Resolve(kstd::string_view name) const {
  const auto *entry = symbols.find(name);
  return entry ? entry->address : 0;
}

}// namespace kernel::driver
