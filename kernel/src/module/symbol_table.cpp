
#include "symbol_table.hpp"

#include "stdkrn.hpp"


namespace kernel::driver {

SymbolTable::~SymbolTable() {
  symbols.clear([](SymbolEntry *entry) {
    delete entry;
  });
}

bool SymbolTable::Register(kstd::string_view name, uintptr_t address) {
  if (const auto entry = KE_CreateObject<SymbolEntry>()) {
    entry->name = name;
    entry->address = address;
    symbols.insert(*entry);
    return true;
  }
  
  return false;
}

uintptr_t SymbolTable::Resolve(kstd::string_view name) const {
  const auto *entry = symbols.find(name);
  return entry ? entry->address : 0;
}

}// namespace kernel::driver
