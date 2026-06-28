#include "vmm.hpp"
#include "memory/slab.hpp"
#include <kernel/hal_interface.hpp>

namespace {
kernel::vmm::AddressSpace g_kernel_address_space;
}// namespace

namespace kernel::vmm {
void init(const BootInfoT &boot_info) noexcept {
  // Initialize the SLAB allocator
  memory::slab_init(boot_info.page_size);

  // Initialize kernel address space
  g_kernel_address_space.translation_root = HAL_VMM_GetCurrentTranslationRoot();
  g_kernel_address_space.asid = 0;
}

AddressSpace *get_kernel_address_space() noexcept {
  return &g_kernel_address_space;
}

void *AddressSpace::allocate_region(size_t size, ProtectFlags flags) noexcept {
  // Basic implementation: use a simple bump allocator for user space for now.
  // In a real OS, this would search the VAD tree for a hole.
  static VirtualAddress user_heap_base = 0x10000000;
  
  VirtualAddress addr = user_heap_base;
  user_heap_base += (size + 4095) & ~static_cast<uintptr_t>(4095);

  auto *vad = static_cast<VirtualAddressDescriptor *>(KE_Malloc(sizeof(VirtualAddressDescriptor)));
  if (vad) {
    vad->start = addr;
    vad->end = addr + size;
    vad->flags = flags;
    vads.insert(vad);
  }

  return addr.as_ptr();
}

void AddressSpace::free_region(void *addr) noexcept {
  auto *vad = vads.find(VirtualAddress::from_ptr(addr));
  if (vad) {
    vads.remove(vad);
    KE_Free(vad);
  }
}

void *AddressSpace::allocate_user_data(size_t size) noexcept {
  return memory::SlabAllocator::instance().allocate_user_memory(*this, size, ProtectFlags::READ | ProtectFlags::WRITE);
}

void *AddressSpace::allocate_user_process(size_t size) noexcept {
  return memory::SlabAllocator::instance().allocate_user_memory(*this, size, ProtectFlags::READ | ProtectFlags::EXECUTE);
}
}// namespace kernel::vmm


UNDOS_KERNEL_API void *KE_Malloc(size_t size) noexcept {
  return kernel::memory::kmalloc(size);
}

UNDOS_KERNEL_API void KE_Free(void *ptr) noexcept {
  kernel::memory::kfree(ptr);
}
