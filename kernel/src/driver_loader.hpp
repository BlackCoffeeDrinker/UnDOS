
#pragma once

#include <Kernel.hpp>
#include <kernel/adt/avl_tree.hpp>
#include <static_string.hpp>
#include <string_view.hpp>

namespace kernel {

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

class SymbolTable {
  adt::AvlTree<SymbolEntry, &SymbolEntry::node> symbols;

  public:
  void Register(kstd::string_view name, uintptr_t address);
  uintptr_t Resolve(kstd::string_view name) const;
};


bool Ke_Driver_Init(const kernel::BootInfoT &boot_info);


enum class ElfResult { Success,
                       InvalidHeader,
                       BoundsViolation,
                       AllocationFailure };

ElfResult Ke_Drv_LoadDriverModule(const uint8_t *raw_blob, size_t blob_size, kernel::KObjectPtr<kernel::KDriverObject> &out_driver);

}// namespace kernel
