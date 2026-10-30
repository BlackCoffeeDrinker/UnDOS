#include "bootinfo_wire.h"

#include <kernel/boot/boot_info.hpp>

#include <stddef.h>
#include <stdint.h>

#define UDL_LAYOUT_ASSERT(expression, message) static_assert(expression, message)

UDL_LAYOUT_ASSERT(sizeof(kernel::MemoryRegionT) == sizeof(udl_boot_memory_region),
                  "memory-region size differs from the GRUB wire layout");
UDL_LAYOUT_ASSERT(alignof(kernel::MemoryRegionT) == 4,
                  "memory-region alignment differs from i386");
UDL_LAYOUT_ASSERT(offsetof(kernel::MemoryRegionT, base) == 0,
                  "memory-region base offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::MemoryRegionT, length) == 4,
                  "memory-region length offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::MemoryRegionT, type) == 12,
                  "memory-region type offset differs");
UDL_LAYOUT_ASSERT(sizeof(kernel::MappedMemoryT) == sizeof(udl_boot_mapped_region),
                  "mapped-region size differs from the GRUB wire layout");
UDL_LAYOUT_ASSERT(alignof(kernel::MappedMemoryT) == 4,
                  "mapped-region alignment differs from i386");
UDL_LAYOUT_ASSERT(offsetof(kernel::MappedMemoryT, type) == 0,
                  "mapped-region type offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::MappedMemoryT, physical_base) == 4,
                  "mapped-region physical-base offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::MappedMemoryT, virtual_base) == 8,
                  "mapped-region virtual-base offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::MappedMemoryT, length) == 12,
                  "mapped-region length offset differs");
UDL_LAYOUT_ASSERT(sizeof(kernel::BootSymbolsT) == sizeof(udl_boot_symbol),
                  "boot-symbol size differs from the GRUB wire layout");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootSymbolsT, name_offset) == 0,
                  "boot-symbol name offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootSymbolsT, address) == 4,
                  "boot-symbol address offset differs");
UDL_LAYOUT_ASSERT(sizeof(kstd::static_string<1024>) == sizeof(udl_boot_command_line),
                  "command-line size differs from the GRUB wire layout");
UDL_LAYOUT_ASSERT(alignof(kstd::static_string<1024>) == 4,
                  "command-line alignment differs from i386");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, page_size) == 0,
                  "BootInfo page_size offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, page_size)
                    == offsetof(udl_boot_info_wire, page_size),
                  "BootInfo page_size differs from the GRUB mirror");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, memory_regions) == 4,
                  "BootInfo memory_regions offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, memory_regions)
                    == offsetof(udl_boot_info_wire, memory_regions),
                  "BootInfo memory_regions differs from the GRUB mirror");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, mapped_memory) == 516,
                  "BootInfo mapped_memory offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, mapped_memory)
                    == offsetof(udl_boot_info_wire, mapped_memory),
                  "BootInfo mapped_memory differs from the GRUB mirror");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, boot_symbol_count) == 676,
                  "BootInfo boot_symbol_count offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, boot_symbol_count)
                    == offsetof(udl_boot_info_wire, boot_symbol_count),
                  "BootInfo boot_symbol_count differs from the GRUB mirror");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, boot_kernel_symbol_count) == 680,
                  "BootInfo boot_kernel_symbol_count offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, boot_kernel_symbol_count)
                    == offsetof(udl_boot_info_wire, boot_kernel_symbol_count),
                  "BootInfo kernel symbol count differs from the GRUB mirror");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, boot_symbol_names_used) == 684,
                  "BootInfo boot_symbol_names_used offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, boot_symbol_names_used)
                    == offsetof(udl_boot_info_wire, boot_symbol_names_used),
                  "BootInfo name usage differs from the GRUB mirror");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, boot_symbols) == 688,
                  "BootInfo boot_symbols offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, boot_symbols)
                    == offsetof(udl_boot_info_wire, boot_symbols),
                  "BootInfo symbols differ from the GRUB mirror");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, boot_symbol_names) == 8880,
                  "BootInfo boot_symbol_names offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, boot_symbol_names)
                    == offsetof(udl_boot_info_wire, boot_symbol_names),
                  "BootInfo names differ from the GRUB mirror");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, command_line) == 58032,
                  "BootInfo command_line offset differs");
UDL_LAYOUT_ASSERT(offsetof(kernel::BootInfoT, command_line)
                    == offsetof(udl_boot_info_wire, command_line),
                  "BootInfo command line differs from the GRUB mirror");
UDL_LAYOUT_ASSERT(sizeof(kernel::BootInfoT) == sizeof(udl_boot_info_wire),
                  "BootInfo size differs from the GRUB wire layout");
UDL_LAYOUT_ASSERT(alignof(kernel::BootInfoT) == 4,
                  "BootInfo alignment differs from i386");
UDL_LAYOUT_ASSERT(sizeof(decltype(kernel::MemoryRegionT::type)) == 1,
                  "memory-region enum size differs");
UDL_LAYOUT_ASSERT(sizeof(decltype(kernel::MappedMemoryT::type)) == 1,
                  "mapped-region enum size differs");
UDL_LAYOUT_ASSERT(
  sizeof(static_cast<kernel::BootInfoT *>(nullptr)->memory_regions) / 32 == 16,
                  "memory-region array element size differs");
UDL_LAYOUT_ASSERT(
  sizeof(static_cast<kernel::BootInfoT *>(nullptr)->mapped_memory) / 10 == 16,
                  "mapped-region array element size differs");
UDL_LAYOUT_ASSERT(
  sizeof(static_cast<kernel::BootInfoT *>(nullptr)->boot_symbols) / 1024 == 8,
                  "boot-symbol array element size differs");
UDL_LAYOUT_ASSERT(sizeof(kernel::BootInfoT) == 59060,
                  "BootInfo size differs from the expected i386 layout");
UDL_LAYOUT_ASSERT(static_cast<uint8_t>(kernel::MemoryRegionType::None) == 0
                    && static_cast<uint8_t>(kernel::MemoryRegionType::Available) == 1
                    && static_cast<uint8_t>(kernel::MemoryRegionType::Reserved) == 2
                    && static_cast<uint8_t>(kernel::MemoryRegionType::ACPI_Reclaim) == 3,
                  "memory-region enum values differ");
UDL_LAYOUT_ASSERT(static_cast<uint8_t>(kernel::MappedMemoryRegionType::None) == 0
                    && static_cast<uint8_t>(kernel::MappedMemoryRegionType::Identity) == 1
                    && static_cast<uint8_t>(kernel::MappedMemoryRegionType::KernelCore) == 2
                    && static_cast<uint8_t>(kernel::MappedMemoryRegionType::HalModule) == 3
                    && static_cast<uint8_t>(kernel::MappedMemoryRegionType::BootStack) == 4
                    && static_cast<uint8_t>(kernel::MappedMemoryRegionType::BootInfo) == 5
                    && static_cast<uint8_t>(kernel::MappedMemoryRegionType::PagingStructures) == 6,
                  "mapped-region enum values differ");

constexpr kernel::BootInfoT make_fixture() {
  kernel::BootInfoT value{};
  value.page_size = 0x12345678;
  value.command_line = "CMDLINE=12";
  return value;
}

extern "C" __attribute__((used, section(".bootinfo_layout_fixture")))
const kernel::BootInfoT bootinfo_layout_fixture = make_fixture();
