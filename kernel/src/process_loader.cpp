
#include "process_loader.hpp"

#include "elf_loader.hpp"
#include "vmm.hpp"

kernel::VirtualAddress kernel::process::load_elf_from_vfs(vmm::AddressSpace &as, const kstd::string_view &path) noexcept {
  kernel::elf::LoadPolicy policy;
  policy.user_mode = true;
  policy.target_as = &as;

  const auto result = kernel::elf::LoadElfFromVfs(path, policy);
  return result.ok ? result.entry_point : VirtualAddress{0};
}
