#ifndef UNDOS_GRUB_DIRECT_BOOTINFO_WIRE_H
#define UNDOS_GRUB_DIRECT_BOOTINFO_WIRE_H

#include "core/types.h"

typedef struct {
  udl_u32 base;
  udl_u32 length_low;
  udl_u32 length_high;
  udl_u8 type;
  udl_u8 padding[3];
} udl_boot_memory_region;

typedef struct {
  udl_u8 type;
  udl_u8 padding[3];
  udl_u32 physical_base;
  udl_u32 virtual_base;
  udl_u32 length;
} udl_boot_mapped_region;

typedef struct {
  udl_u32 name_offset;
  udl_u32 address;
} udl_boot_symbol;

typedef struct {
  udl_u32 length;
  char buffer[1024];
} udl_boot_command_line;

typedef struct {
  udl_u32 page_size;
  udl_boot_memory_region memory_regions[32];
  udl_boot_mapped_region mapped_memory[10];
  udl_u32 boot_symbol_count;
  udl_u32 boot_kernel_symbol_count;
  udl_u32 boot_symbol_names_used;
  udl_boot_symbol boot_symbols[1024];
  char boot_symbol_names[48 * 1024];
  udl_boot_command_line command_line;
} udl_boot_info_wire;

#endif
