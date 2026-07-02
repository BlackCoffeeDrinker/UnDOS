
#pragma once

#include <kernel/__core.hpp>

UNDOS_KERNEL_API bool KE_OB_InsertObject(const kernel::KObjectPtr<kernel::KDirectoryObject>& parent, const kernel::KObjectPtr<kernel::KObject>& child) noexcept;
UNDOS_KERNEL_API kernel::KObjectPtr<kernel::KObject> KE_OB_LookupObject(kstd::string_view path) noexcept;
UNDOS_KERNEL_API kernel::KObjectPtr<kernel::KDirectoryObject> KE_OB_GetRootDirectory() noexcept;

// Helper
template<typename T, kernel::ObjectType requested_type = T::Type>
kernel::KObjectPtr<T> KE_OB_LookupObjectOfType(kstd::string_view path) noexcept {
  if (auto obj = KE_OB_LookupObject(path)) {
    if (obj->type == requested_type)
      return kernel::KObjectPtr<T>(static_cast<T*>(obj.release()));
  }

  return nullptr;
}
