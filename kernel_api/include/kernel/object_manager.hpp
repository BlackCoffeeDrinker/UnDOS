
#pragma once

#include <kernel/__core.hpp>

UNDOS_KERNEL_API bool KE_Ob_InsertObject(kernel::KDirectoryObject *parent, kernel::KObject *child) noexcept;
UNDOS_KERNEL_API kernel::KObjectPtr<kernel::KObject> KE_Ob_LookupObject(kstd::string_view path) noexcept;
UNDOS_KERNEL_API kernel::KDirectoryObject *KE_Ob_GetRootDirectory() noexcept;
