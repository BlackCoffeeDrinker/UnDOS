
#include "symbol_table.hpp"

#include "stdkrn.hpp"


namespace kernel::driver {

SymbolTable::~SymbolTable() {
  symbols.clear([](SymbolEntry *entry) {
    entry->~SymbolEntry();
    KE_Free(entry);
  });
}

kstd::error_code SymbolTable::Register(const kstd::string_view& name, uintptr_t address) {
  if (name.empty()) return KernelError::InvalidArgument;
  if (name.size() > decltype(SymbolEntry::name)::capacity()) return KernelError::NameTooLong;
  if (const auto *existing = symbols.find(name)) {
    KE_Print("SymbolTable: duplicate symbol '{}' (existing: {x}, incoming: {x})",
             name, existing->address, address);
    return KernelError::AlreadyExists;
  }
  auto entry = KE_CreateObject<SymbolEntry>();
  if (!entry) return entry.error();

  entry.value()->name = name;
  entry.value()->address = address;
  symbols.insert(*entry.value());

  // The AVL tree now owns this memory (linked in by address); release the
  // unique_ptr so it isn't freed here out from under the tree. It is freed
  // later via KE_Free in the destructor's clear() callback above.
  entry->release();
  return KernelError::Success;
}

 kstd::expected<uintptr_t, kstd::error_code> SymbolTable::Resolve(kstd::string_view name) const {
  const auto *entry = symbols.find(name);
  if (!entry) return kstd::unexpected(KernelError::NotFound);
  return entry->address;
}

}// namespace kernel::driver
