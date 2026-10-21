#pragma once

#include <kernel/__core.hpp>
#include <kernel/cfunc.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>
#include <kernel/module/export_table.hpp>

#include <string.hpp>
#include <vector.hpp>

// Marks a symbol of a loadable module as visible to other modules. Modules
// build with -fvisibility=hidden, so only marked global symbols are exported.
#define UNDOS_MODULE_EXPORT extern "C" __attribute__((visibility("default")))

namespace kernel {

struct KModuleObject : KObjectT<KModuleObject, 1, TYPE_MODULE> {
  using KObjectT::KObjectT;
  PhysicalAddress base_physical;
  size_t length;
  VirtualAddress load_base{0};
  size_t total_size{0};
  kstd::vector<kstd::string> dependencies;// modules that were loaded first
  ExportTable exports;                    // GLOBAL, default-visibility symbols of the image
  cfunc<void(const KObjectPtr<KModuleObject> &)> entry_point;

};

template<>
struct ObjectTypeOf<TYPE_MODULE> {
  using type = KModuleObject;
};

} // namespace kernel
