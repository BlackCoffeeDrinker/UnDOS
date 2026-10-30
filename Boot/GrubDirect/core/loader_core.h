#ifndef UNDOS_GRUB_DIRECT_LOADER_CORE_H
#define UNDOS_GRUB_DIRECT_LOADER_CORE_H

#include "types.h"

#define UDL_PAGE_SIZE 4096u
#define UDL_KERNEL_BASE 0xc0000000u
#define UDL_HAL_LINK_BASE 0x80000000u
#define UDL_VIRTUAL_LIMIT 0xc0800000u
#define UDL_MAX_SECTIONS 4096u
#define UDL_MAX_SYMBOLS 65536u
#define UDL_MAX_RELOCATIONS 1048576u
#define UDL_MAX_EXPORTS 1024u
#define UDL_NAME_POOL_SIZE (48u * 1024u)
#define UDL_MAX_MEMORY_REGIONS 32u
#define UDL_MAX_RAW_MEMORY_REGIONS 256u

typedef enum {
  UDL_OK = 0,
  UDL_BAD_ARGUMENT,
  UDL_BAD_ELF,
  UDL_UNSUPPORTED_ELF,
  UDL_CAPACITY,
  UDL_BAD_MEMORY_MAP,
  UDL_NO_MEMORY,
  UDL_UNRESOLVED_SYMBOL,
  UDL_BAD_RELOCATION,
  UDL_BAD_LAYOUT,
} udl_error;

enum {
  UDL_ROLE_KERNEL = 0,
  UDL_ROLE_HAL = 1,
  UDL_MEMORY_NONE = 0,
  UDL_MEMORY_AVAILABLE = 1,
  UDL_MEMORY_RESERVED = 2,
  UDL_MEMORY_ACPI_RECLAIM = 3,
  UDL_GRUB_MEMORY_AVAILABLE = 1,
  UDL_GRUB_MEMORY_ACPI = 3,
};

typedef struct {
  const udl_u8 *bytes;
  udl_size size;
} udl_blob;

typedef struct {
  udl_u32 name;
  udl_u32 type;
  udl_u32 flags;
  udl_u32 address;
  udl_u32 offset;
  udl_u32 size;
  udl_u32 link;
  udl_u32 info;
  udl_u32 alignment;
  udl_u32 entry_size;
} udl_section;

typedef struct {
  udl_u32 site;
  udl_u32 value;
} udl_write_slot;

typedef struct {
  udl_blob blob;
  udl_section *sections;
  udl_size section_capacity;
  udl_u32 linked_base;
  udl_u32 runtime_base;
  udl_u32 end;
  udl_u32 span;
  udl_u32 entry;
  udl_u32 program_offset;
  udl_u16 program_count;
  udl_u32 section_count;
  udl_u32 symbol_section;
  udl_u32 string_section;
  udl_u32 got_section;
  udl_u32 got_linked_base;
  udl_u32 got_size;
  udl_u32 bias;
  udl_u8 role;
} udl_image;

typedef struct {
  const char *name;
  udl_u32 address;
  udl_u32 name_offset;
  udl_u8 image;
} udl_export;

typedef struct {
  udl_export *exports;
  udl_size export_capacity;
  udl_size export_count;
  char *name_pool;
  udl_size name_capacity;
  udl_size names_used;
  udl_size kernel_export_count;
  udl_u32 duplicate_exports;
} udl_export_table;

typedef struct {
  udl_u64 base;
  udl_u64 length;
  udl_u32 type;
} udl_raw_memory_region;

typedef struct {
  udl_u32 base;
  udl_u32 length;
  udl_u8 type;
  udl_u8 padding[3];
} udl_memory_region;

typedef struct {
  udl_memory_region regions[UDL_MAX_MEMORY_REGIONS];
  udl_size count;
} udl_memory_map;

typedef struct {
  udl_image kernel;
  udl_image hal;
  udl_u32 kernel_end;
  udl_u32 hal_base;
  udl_u32 hal_end;
  udl_u32 stack_bottom;
  udl_u32 stack_top;
  udl_u32 bootinfo_base;
  udl_u32 bootinfo_end;
  udl_u32 payload_size;
  udl_u32 kernel_entry;
} udl_layout;

udl_error udl_image_open(udl_image *image, udl_blob blob, udl_section *sections,
                         udl_size section_capacity, udl_u8 role);
udl_error udl_plan_layout(udl_image *kernel, udl_image *hal,
                          udl_u32 bootinfo_size, udl_layout *layout);
udl_error udl_collect_exports(udl_image *kernel, udl_image *hal,
                             udl_export_table *table);
udl_error udl_image_relocation_count(const udl_image *image,
                                     udl_u32 *relocation_count);
udl_error udl_link_images(udl_image *kernel, udl_image *hal,
                          udl_export_table *exports,
                          udl_u8 *kernel_destination, udl_size kernel_size,
                          udl_u8 *hal_destination, udl_size hal_size,
                          udl_write_slot *writes, udl_size write_capacity);
udl_error udl_normalize_memory_map(const udl_raw_memory_region *raw,
                                   udl_size raw_count,
                                   udl_memory_map *output);
udl_error udl_choose_physical_base(const udl_memory_map *map,
                                   udl_u32 span, udl_u32 *base);
const char *udl_error_name(udl_error error);

#endif
