#pragma once

#include <kernel/kerror.hpp>
#include <kernel/memory/address.hpp>
#include <expected.hpp>
#include <string_view.hpp>

namespace kernel::vmm {
struct AddressSpace;
}// namespace kernel::vmm

// Kernel-internal PS extension point. PS knows nothing about image formats;
// the loader registers here and PS only sees an entry point.
namespace kernel::ps {

// Maps the user-mode image at `path` into `target_as`; returns its entry point.
using ImageLoader = kstd::expected<VirtualAddress, kstd::error_code> (*)(
    const kstd::string_view &path, vmm::AddressSpace &target_as) noexcept;

void SetImageLoader(ImageLoader loader) noexcept;

// Unsupported when no loader is registered.
kstd::expected<VirtualAddress, kstd::error_code> LoadImage(
    const kstd::string_view &path, vmm::AddressSpace &target_as) noexcept;

}// namespace kernel::ps
