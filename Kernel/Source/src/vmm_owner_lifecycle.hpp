#pragma once

#include "vmm.hpp"

namespace kernel::vmm::detail {

template <typename RegisterOwner, typename CreateRoot, typename SetOwner,
          typename DestroyRoot, typename UnregisterOwner>
kstd::error_code create_owned_address_space_root(
    AddressSpace &out_as, uint32_t owner_pid, RegisterOwner &&register_owner,
    CreateRoot &&create_root, SetOwner &&set_owner, DestroyRoot &&destroy_root,
    UnregisterOwner &&unregister_owner) noexcept {
  if (out_as.translation_root &&
      out_as.translation_root != PhysicalAddress::Invalid)
    return KernelError::InvalidState;
  if (!owner_pid || !register_owner(owner_pid))
    return KernelError::ResourceLimitExceeded;

  const PhysicalAddress root = create_root();
  if (!root) {
    unregister_owner(owner_pid);
    return KernelError::OutOfMemory;
  }
  if (!set_owner(root, owner_pid)) {
    destroy_root(root);
    unregister_owner(owner_pid);
    return KernelError::ResourceLimitExceeded;
  }

  out_as.vads = VadTree();
  out_as.translation_root = root;
  out_as.asid = 0;
  out_as.owner_pid = owner_pid;
  out_as.current_base = 0x10000000;
  out_as.limit = 0xC0000000;
  return {};
}

}  // namespace kernel::vmm::detail
