#include "bootinfo_wire.h"
#include "core/loader_core.h"
#include "core/cpu_profile.h"
#include "core/diagnostics.h"
#include "transition_layout.h"

#include <grub/command.h>
#include <grub/dl.h>
#include <grub/env.h>
#include <grub/err.h>
#include <grub/file.h>
#include <grub/i386/relocator.h>
#include <grub/loader.h>
#include <grub/memory.h>
#include <grub/misc.h>
#include <grub/mm.h>
#include <grub/relocator.h>
#include <grub/relocator_private.h>
#include <grub/term.h>

GRUB_MOD_LICENSE("GPLv3+");

#define UDL_INPUT_LIMIT (32u * 1024u * 1024u)
#define UDL_BOOTSTRAP_PAGES 12u
#define UDL_BOOTSTRAP_SIZE (UDL_BOOTSTRAP_PAGES * UDL_PAGE_SIZE)
#define UDL_SEARCH_LIMIT 4096u
#define UDL_NO_ADDRESS 0xffffffffu

enum {
  UDL_MAPPED_KERNEL = 2,
  UDL_MAPPED_HAL = 3,
  UDL_MAPPED_STACK = 4,
  UDL_MAPPED_BOOTINFO = 5,
  UDL_MAPPED_PAGING = 6,
};

struct udl_transition_parameters {
  grub_uint16_t gdt_limit;
  grub_uint32_t gdt_base;
  grub_uint32_t code_offset;
  grub_uint16_t code_selector;
  grub_uint32_t page_directory;
  grub_uint32_t temporary_stack_top;
  grub_uint32_t high_stack_top;
  grub_uint32_t bootinfo_virtual;
  grub_uint32_t kernel_entry;
  grub_uint32_t cpu_cleanup_mask;
  grub_uint32_t diagnostic_stop;
  grub_uint32_t diagnostic_text;
} __attribute__((packed));

struct udl_cpu_probe_result {
  grub_uint32_t id_flag_mutable;
  grub_uint32_t maximum_basic_leaf;
  grub_uint32_t leaf1_edx;
};

struct udl_map_capture {
  udl_raw_memory_region regions[UDL_MAX_RAW_MEMORY_REGIONS];
  udl_size count;
  int overflow;
};

struct udl_loader_context {
  struct grub_relocator *relocator;
  grub_relocator_chunk_t payload_chunk;
  void *payload_current;
  grub_phys_addr_t payload_target;
  grub_phys_addr_t transition_target;
  grub_phys_addr_t raw_gate_target;
  grub_phys_addr_t parameter_target;
  grub_size_t total_size;
  grub_uint32_t cpu_cleanup_mask;
  grub_uint32_t diagnostic_stop;
  int module_reference;
  int boot_failed;
};

static grub_dl_t udl_module;
static grub_command_t udl_command_handle;
static grub_command_t udl_diag_command_handle;
static struct udl_loader_context *udl_active;

extern grub_uint8_t udl_transition_start[];
extern grub_uint8_t udl_transition_after_far[];
extern grub_uint8_t udl_transition_end[];
extern grub_uint8_t udl_raw_gate_start[];
extern grub_uint8_t udl_raw_gate_end[];
extern grub_err_t udl_cpu_preflight(struct udl_cpu_probe_result *result);
extern void udl_transfer_relocations(void *relocations)
  __attribute__((noreturn));

static void
udl_diag_stop_forever(void)
  __attribute__((noreturn));

static void
udl_diag_stop_forever(void)
{
  __asm__ volatile ("cli\n1: jmp 1b" ::: "memory");
  __builtin_unreachable();
}

static void
udl_diag_write_row(grub_uint32_t row, const char *text)
{
  volatile grub_uint16_t *vga =
    (volatile grub_uint16_t *) (grub_addr_t) 0x000b8000u;
  grub_uint32_t offset = row * 80;
  for (grub_uint32_t column = 0; column < 80; ++column)
    vga[offset + column] = 0x4f20;
  for (grub_uint32_t column = 0; column < 80 && text[column]; ++column)
    vga[offset + column] =
      (grub_uint16_t) (0x4f00u | (grub_uint8_t) text[column]);
}

static grub_uint8_t
udl_diag_read_physical_byte(grub_uint32_t address)
{
  grub_uint8_t value;
  __asm__ volatile ("movb (%1), %0"
                    : "=q" (value)
                    : "r" ((grub_addr_t) address)
                    : "memory");
  return value;
}

static grub_err_t
udl_diag_confirm_video(void)
{
  const grub_uint8_t mode = udl_diag_read_physical_byte(0x449);
  const grub_uint16_t columns =
    (grub_uint16_t) udl_diag_read_physical_byte(0x44a)
    | ((grub_uint16_t) udl_diag_read_physical_byte(0x44b) << 8);
  const grub_uint8_t active_page = udl_diag_read_physical_byte(0x462);
  if (!udl_diag_video_profile_supported(mode, columns, active_page))
    {
      grub_printf("direct-loader diagnostics require VGA mode 3, 80 columns, page 0 (mode=%u columns=%u page=%u)\n",
                  mode, columns, active_page);
      (void) grub_getkey();
      return grub_error(GRUB_ERR_BAD_OS,
                        "direct-loader diagnostic VGA profile is unsupported");
    }

  udl_diag_write_row(
    23, "UDL VGA TEST: press Enter only if this direct-memory banner is visible");
  grub_printf("direct-loader: VGA text banner is displayed; press Enter only if visible\n");
  {
    const int key = grub_getkey();
    if (key != '\n' && key != '\r')
      return grub_error(GRUB_ERR_BAD_ARGUMENT,
                        "direct-loader VGA diagnostic was not confirmed");
  }
  udl_diag_write_row(23, "UDL VGA TEST CONFIRMED: direct text aperture 0xB8000");
  return GRUB_ERR_NONE;
}

static void
udl_diag_hold(grub_uint32_t stop, grub_uint32_t stage)
  __attribute__((noreturn));

static void
udl_diag_hold(grub_uint32_t stop, grub_uint32_t stage)
{
  volatile grub_uint16_t *vga =
    (volatile grub_uint16_t *) (grub_addr_t) 0x000b8000u;
  const grub_uint32_t row_offset = 24 * 80;
  grub_uint32_t column = 0;
  const char *prefixes[3] = {
    "UDL DIAG ", udl_diag_names[stage], " STOP="
  };
  const char *stop_name = stop > 0 && stop <= UDL_DIAG_STOP_COUNT
    ? udl_diag_names[stop - 1] : "invalid";
  for (grub_uint32_t i = 0; i < 80; ++i)
    vga[row_offset + i] = 0x4f20;
  for (grub_uint32_t part = 0; part < 3; ++part)
    for (const char *text = prefixes[part]; *text && column < 80; ++text)
      vga[row_offset + column++] =
        (grub_uint16_t) (0x4f00u | (grub_uint8_t) *text);
  for (const char *text = stop_name; *text && column < 80; ++text)
    vga[row_offset + column++] =
      (grub_uint16_t) (0x4f00u | (grub_uint8_t) *text);
  for (const char *text = " RESET AFTER OBSERVATION";
       *text && column < 80; ++text)
    vga[row_offset + column++] =
      (grub_uint16_t) (0x4f00u | (grub_uint8_t) *text);
  udl_diag_stop_forever();
}

static grub_err_t
udl_capture_cpu_profile(grub_uint32_t *cleanup_mask)
{
  struct udl_cpu_probe_result probe;
  udl_error error;

  if (udl_cpu_preflight(&probe) != GRUB_ERR_NONE)
    return grub_error(GRUB_ERR_BAD_OS,
                      "direct-loader requires BIOS flat protected mode with paging off");
  error = udl_cpu_cleanup_mask(probe.id_flag_mutable,
                               probe.maximum_basic_leaf,
                               probe.leaf1_edx, cleanup_mask);
  if (error != UDL_OK)
    return grub_error(GRUB_ERR_BAD_OS,
                      "direct-loader CPU feature profile is invalid");
  return GRUB_ERR_NONE;
}

static int
udl_map_hook(grub_uint64_t base, grub_uint64_t length,
             grub_memory_type_t type, void *data)
{
  struct udl_map_capture *capture = data;
  if (capture->count == UDL_MAX_RAW_MEMORY_REGIONS)
    {
      capture->overflow = 1;
      return 1;
    }
  capture->regions[capture->count].base = base;
  capture->regions[capture->count].length = length;
  capture->regions[capture->count].type = (udl_u32) type;
  ++capture->count;
  return 0;
}

static grub_err_t
udl_read_image(const char *path, enum grub_file_type file_type,
               const char *role, grub_file_t *file_out, void **buffer_out,
               grub_size_t *size_out)
{
  grub_file_t file;
  grub_off_t file_size;
  grub_size_t total = 0;
  void *buffer;

  file = grub_file_open(path, file_type);
  if (!file)
    return grub_error(grub_errno, "%s image could not be opened", role);
  *file_out = file;
  file_size = grub_file_size(file);
  if (file_size == GRUB_FILE_SIZE_UNKNOWN || file_size == 0
      || file_size > UDL_INPUT_LIMIT || file_size > 0xffffffffu)
    return grub_error(GRUB_ERR_OUT_OF_RANGE,
                      "%s image has an unknown or excessive decompressed size",
                      role);
  buffer = grub_malloc((grub_size_t) file_size);
  if (!buffer)
    return grub_errno;
  *buffer_out = buffer;
  while (total < (grub_size_t) file_size)
    {
      const grub_ssize_t count = grub_file_read(
          file, (grub_uint8_t *) buffer + total,
          (grub_size_t) file_size - total);
      if (count <= 0)
        return grub_error(GRUB_ERR_FILE_READ_ERROR,
                          "%s image read was incomplete", role);
      total += (grub_size_t) count;
    }
  *size_out = total;
  return GRUB_ERR_NONE;
}

static grub_err_t
udl_close_file(grub_file_t *file, const char *role)
{
  grub_file_t value = *file;
  grub_err_t error;
  if (!value)
    return GRUB_ERR_NONE;
  *file = 0;
  error = grub_file_close(value);
  if (error != GRUB_ERR_NONE)
    return grub_error(error, "%s image close failed", role);
  return GRUB_ERR_NONE;
}

static void
udl_release_context(struct udl_loader_context *context)
{
  int preserve_error = grub_errno != GRUB_ERR_NONE;
  if (preserve_error)
    grub_error_push();
  if (context)
    {
      if (context->relocator)
        grub_relocator_unload(context->relocator);
      if (context->module_reference)
        {
          context->module_reference = 0;
          grub_dl_unref(udl_module);
        }
      grub_free(context);
    }
  if (preserve_error)
    grub_error_pop();
}

static int
udl_range_is_usable(const udl_memory_map *map, grub_uint32_t start,
                    grub_uint32_t length)
{
  const grub_uint64_t end = (grub_uint64_t) start + length;
  for (udl_size i = 0; i < map->count; ++i)
    {
      const udl_memory_region *region = &map->regions[i];
      const grub_uint64_t region_end =
        (grub_uint64_t) region->base + region->length;
      if (region->type == UDL_MEMORY_AVAILABLE
          && start >= region->base && end <= region_end)
        return 1;
    }
  return 0;
}

static grub_err_t
udl_try_target(struct udl_loader_context *context, grub_uint32_t target,
               grub_uint32_t length)
{
  grub_err_t error;
  grub_phys_addr_t actual_target;
  grub_error_push();
  error = grub_relocator_alloc_chunk_addr(context->relocator,
      &context->payload_chunk, (grub_phys_addr_t) target,
      (grub_size_t) length);
  grub_error_pop();
  if (error != GRUB_ERR_NONE)
    return error;
  actual_target = get_physical_target_address(context->payload_chunk);
  if (actual_target != (grub_phys_addr_t) target
      || actual_target > 0x20000000u
      || length > 0x20000000u - actual_target)
    return grub_error(GRUB_ERR_OUT_OF_RANGE,
                      "GRUB relocator returned an invalid physical target");
  context->payload_current =
    get_virtual_current_address(context->payload_chunk);
  if (!context->payload_current)
    return grub_error(GRUB_ERR_OUT_OF_MEMORY,
                      "GRUB relocator returned no writable backing");
  context->payload_target = actual_target;
  context->total_size = (grub_size_t) length;
  return GRUB_ERR_NONE;
}

static grub_err_t
udl_allocate_payload(struct udl_loader_context *context,
                     const udl_memory_map *map, grub_uint32_t length)
{
  grub_uint32_t candidate = UDL_NO_ADDRESS;
  grub_uint32_t attempts = 0;
  if (udl_choose_physical_base(map, length, &candidate) == UDL_OK
      && udl_range_is_usable(map, candidate, length))
    {
      grub_err_t error = udl_try_target(context, candidate, length);
      if (error == GRUB_ERR_NONE || context->payload_chunk)
        return error;
    }

  for (udl_size i = map->count; i > 0 && attempts < UDL_SEARCH_LIMIT; --i)
    {
      const udl_memory_region *region = &map->regions[i - 1];
      grub_uint64_t lower;
      grub_uint64_t end;
      grub_uint64_t address;
      if (region->type != UDL_MEMORY_AVAILABLE)
        continue;
      lower = region->base < 0x00100000u ? 0x00100000u : region->base;
      end = (grub_uint64_t) region->base + region->length;
      if (end < lower || end - lower < length)
        continue;
      address = (end - length) & ~((grub_uint64_t) UDL_PAGE_SIZE - 1);
      while (address >= lower && address + length <= end
             && address + length <= 0x20000000ull
             && attempts++ < UDL_SEARCH_LIMIT)
        {
          if (address != candidate)
            {
              grub_err_t error =
                udl_try_target(context, (grub_uint32_t) address, length);
              if (error == GRUB_ERR_NONE || context->payload_chunk)
                return error;
            }
          if (address < lower + UDL_PAGE_SIZE)
            break;
          address -= UDL_PAGE_SIZE;
        }
    }
  return grub_error(GRUB_ERR_OUT_OF_MEMORY,
                    "no usable physical span for direct-loader payload");
}

static grub_err_t
udl_set_bootinfo(udl_boot_info_wire *bootinfo, const udl_layout *layout,
                 const udl_memory_map *map, const udl_export_table *exports,
                 const char *command_line, grub_phys_addr_t payload_target,
                 grub_phys_addr_t bootstrap_target)
{
  grub_size_t line_length = grub_strlen(command_line);
  bootinfo->page_size = UDL_PAGE_SIZE;
  for (udl_size i = 0; i < map->count; ++i)
    {
      udl_boot_memory_region *target = &bootinfo->memory_regions[i];
      target->base = map->regions[i].base;
      target->length_low = map->regions[i].length;
      target->length_high = 0;
      target->type = map->regions[i].type;
    }
  bootinfo->mapped_memory[0].type = UDL_MAPPED_KERNEL;
  bootinfo->mapped_memory[0].physical_base =
    (grub_uint32_t) payload_target;
  bootinfo->mapped_memory[0].virtual_base = UDL_KERNEL_BASE;
  bootinfo->mapped_memory[0].length = layout->kernel_end - UDL_KERNEL_BASE;
  bootinfo->mapped_memory[1].type = UDL_MAPPED_HAL;
  bootinfo->mapped_memory[1].physical_base =
    (grub_uint32_t) payload_target + layout->hal_base - UDL_KERNEL_BASE;
  bootinfo->mapped_memory[1].virtual_base = layout->hal_base;
  bootinfo->mapped_memory[1].length = layout->hal_end - layout->hal_base;
  bootinfo->mapped_memory[2].type = UDL_MAPPED_STACK;
  bootinfo->mapped_memory[2].physical_base =
    (grub_uint32_t) payload_target + layout->stack_bottom - UDL_KERNEL_BASE;
  bootinfo->mapped_memory[2].virtual_base = layout->stack_bottom;
  bootinfo->mapped_memory[2].length = layout->stack_top - layout->stack_bottom;
  bootinfo->mapped_memory[3].type = UDL_MAPPED_BOOTINFO;
  bootinfo->mapped_memory[3].physical_base =
    (grub_uint32_t) payload_target + layout->bootinfo_base - UDL_KERNEL_BASE;
  bootinfo->mapped_memory[3].virtual_base = layout->bootinfo_base;
  bootinfo->mapped_memory[3].length =
    layout->bootinfo_end - layout->bootinfo_base;
  bootinfo->mapped_memory[4].type = UDL_MAPPED_PAGING;
  bootinfo->mapped_memory[4].physical_base = (grub_uint32_t) bootstrap_target;
  bootinfo->mapped_memory[4].virtual_base = (grub_uint32_t) bootstrap_target;
  bootinfo->mapped_memory[4].length = UDL_BOOTSTRAP_SIZE;
  for (udl_size i = 0; i < exports->export_count; ++i)
    {
      const udl_export *source = &exports->exports[i];
      udl_boot_symbol *symbol = &bootinfo->boot_symbols[i];
      grub_size_t length = grub_strlen(source->name) + 1;
      if (source->name_offset > sizeof(bootinfo->boot_symbol_names)
          || length > sizeof(bootinfo->boot_symbol_names)
            - source->name_offset)
        return grub_error(GRUB_ERR_OUT_OF_RANGE,
                          "direct-loader BootInfo symbol names exceed capacity");
      symbol->name_offset = source->name_offset;
      symbol->address = source->address;
      grub_memcpy(bootinfo->boot_symbol_names + source->name_offset,
                  source->name, length);
    }
  bootinfo->boot_symbol_count = exports->export_count;
  bootinfo->boot_kernel_symbol_count = exports->kernel_export_count;
  bootinfo->boot_symbol_names_used = exports->names_used;
  if (line_length > 1023)
    return grub_error(GRUB_ERR_BAD_ARGUMENT,
                      "direct-loader command line exceeds 1023 bytes");
  bootinfo->command_line.length = line_length;
  grub_memcpy(bootinfo->command_line.buffer, command_line, line_length + 1);
  return GRUB_ERR_NONE;
}

static grub_err_t
udl_build_page_tables(struct udl_loader_context *context,
                      const udl_layout *layout,
                      grub_phys_addr_t bootstrap_target,
                      grub_size_t transition_size,
                      grub_uint32_t cpu_cleanup_mask,
                      grub_uint32_t diagnostic_stop,
                      grub_phys_addr_t *transition_target,
                      grub_phys_addr_t *parameter_target)
{
  grub_uint32_t *directory;
  grub_uint32_t *tables;
  grub_uint32_t *identity[4];
  grub_uint32_t *high[2];
  grub_uint32_t *extra[2];
  grub_uint32_t extra_pde[2] = { UDL_NO_ADDRESS, UDL_NO_ADDRESS };
  grub_uint32_t extra_count = 0;
  grub_uint32_t page_directory_phys = (grub_uint32_t) bootstrap_target;
  grub_uint32_t bootstrap_end = page_directory_phys + UDL_BOOTSTRAP_SIZE;
  grub_uint8_t *bootstrap_current =
    (grub_uint8_t *) context->payload_current + layout->payload_size;
  grub_uint32_t *gdt;
  struct udl_transition_parameters *parameters;
  grub_uint16_t *gdt_descriptor;
  grub_uint8_t *transition_current;
  grub_uint8_t *raw_gate_current;
  grub_size_t raw_gate_size =
    (grub_size_t) (udl_raw_gate_end - udl_raw_gate_start);

  if (transition_size == 0
      || transition_size > UDL_TRANSITION_RAW_GATE_OFFSET
                       - UDL_TRANSITION_ENTRY_OFFSET
      || raw_gate_size == 0
      || raw_gate_size > UDL_TRANSITION_DIAG_NAMES_OFFSET
                      - UDL_TRANSITION_RAW_GATE_OFFSET
      || bootstrap_end < page_directory_phys
      || bootstrap_end > 0x20000000u)
    return grub_error(GRUB_ERR_OUT_OF_RANGE,
                      "direct-loader bootstrap layout exceeds its bound");
  grub_memset(bootstrap_current, 0, UDL_BOOTSTRAP_SIZE);
  tables = (grub_uint32_t *) bootstrap_current;
  directory = tables;
  for (grub_uint32_t i = 0; i < 4; ++i)
    identity[i] = tables + (1 + i) * 1024;
  high[0] = tables + 5 * 1024;
  high[1] = tables + 6 * 1024;
  extra[0] = tables + 7 * 1024;
  extra[1] = tables + 8 * 1024;
  for (grub_uint32_t pde = 0; pde < 4; ++pde)
    {
      directory[pde] =
        (page_directory_phys + (1 + pde) * UDL_PAGE_SIZE) | 3;
      for (grub_uint32_t pte = 0; pte < 1024; ++pte)
        identity[pde][pte] =
          (pde * 1024 + pte) * UDL_PAGE_SIZE | 3;
    }
  for (grub_uint32_t virtual_address = UDL_KERNEL_BASE;
       virtual_address < layout->bootinfo_end;
       virtual_address += UDL_PAGE_SIZE)
    {
      const grub_uint32_t pde = virtual_address >> 22;
      const grub_uint32_t pte = (virtual_address >> 12) & 0x3ff;
      grub_uint32_t *table;
      if (pde < 768 || pde > 769)
        return grub_error(GRUB_ERR_OUT_OF_RANGE,
                          "direct-loader high-half mapping exceeds two tables");
      table = high[pde - 768];
      table[pte] = ((grub_uint32_t) context->payload_target
                    + virtual_address - UDL_KERNEL_BASE) | 3;
      directory[pde] =
        ((grub_uint32_t) bootstrap_target + (5 + pde - 768) * UDL_PAGE_SIZE)
        | 3;
    }
  for (grub_uint32_t physical = page_directory_phys;
       physical < bootstrap_end; physical += UDL_PAGE_SIZE)
    {
      const grub_uint32_t pde = physical >> 22;
      const grub_uint32_t pte = (physical >> 12) & 0x3ff;
      grub_uint32_t *table;
      if (pde < 4)
        table = identity[pde];
      else
        {
          grub_uint32_t slot;
          for (slot = 0; slot < extra_count; ++slot)
            if (extra_pde[slot] == pde)
              break;
          if (slot == extra_count)
            {
              if (extra_count == 2)
                return grub_error(GRUB_ERR_OUT_OF_RANGE,
                                  "direct-loader bootstrap needs too many identity tables");
              extra_pde[extra_count] = pde;
              table = extra[extra_count];
              directory[pde] = (page_directory_phys
                                + (7 + extra_count) * UDL_PAGE_SIZE) | 3;
              ++extra_count;
            }
          else
            table = extra[slot];
        }
      table[pte] = physical | 3;
    }
  if (10 + extra_count > UDL_BOOTSTRAP_PAGES)
    return grub_error(GRUB_ERR_OUT_OF_RANGE,
                      "direct-loader bootstrap page capacity exceeded");
  directory[1023] = page_directory_phys | 3;

  transition_current = bootstrap_current + 9 * UDL_PAGE_SIZE
    + UDL_TRANSITION_ENTRY_OFFSET;
  grub_memcpy(transition_current, udl_transition_start, transition_size);
  raw_gate_current = bootstrap_current + 9 * UDL_PAGE_SIZE
    + UDL_TRANSITION_RAW_GATE_OFFSET;
  grub_memcpy(raw_gate_current, udl_raw_gate_start, raw_gate_size);
  grub_memcpy(bootstrap_current + 9 * UDL_PAGE_SIZE
                + UDL_TRANSITION_DIAG_NAMES_OFFSET,
              udl_diag_names, sizeof(udl_diag_names));
  *transition_target = bootstrap_target + 9 * UDL_PAGE_SIZE
    + UDL_TRANSITION_ENTRY_OFFSET;
  parameters = (struct udl_transition_parameters *)
    (bootstrap_current + 9 * UDL_PAGE_SIZE
     + UDL_TRANSITION_PARAMETERS_OFFSET);
  gdt = (grub_uint32_t *)
    (bootstrap_current + 9 * UDL_PAGE_SIZE + UDL_TRANSITION_GDT_OFFSET);
  gdt[0] = 0;
  gdt[1] = 0;
  gdt[2] = 0;
  gdt[3] = 0;
  gdt[4] = 0x0000ffff;
  gdt[5] = 0x00cf9a00;
  gdt[6] = 0x0000ffff;
  gdt[7] = 0x00cf9200;
  gdt_descriptor = (grub_uint16_t *) parameters;
  gdt_descriptor[0] = 63;
  *((grub_uint32_t *) ((grub_uint8_t *) parameters + 2)) =
    bootstrap_target + 9 * UDL_PAGE_SIZE + 128;
  parameters->code_offset = *transition_target
    + (grub_uint32_t) (udl_transition_after_far - udl_transition_start);
  parameters->code_selector = 0x10;
  parameters->page_directory = page_directory_phys;
  parameters->temporary_stack_top = bootstrap_end;
  parameters->high_stack_top =
    layout->stack_top;
  parameters->bootinfo_virtual = layout->bootinfo_base;
  parameters->kernel_entry = layout->kernel_entry;
  parameters->cpu_cleanup_mask = cpu_cleanup_mask;
  parameters->diagnostic_stop = diagnostic_stop;
  parameters->diagnostic_text = (grub_uint32_t) bootstrap_target
    + 9 * UDL_PAGE_SIZE + UDL_TRANSITION_DIAG_NAMES_OFFSET;
  *parameter_target = bootstrap_target + 9 * UDL_PAGE_SIZE
    + UDL_TRANSITION_PARAMETERS_OFFSET;
  if (*transition_target - *parameter_target
      != UDL_TRANSITION_PARAMETERS_DELTA)
    return grub_error(GRUB_ERR_BUG,
                      "direct-loader transition parameter fixup is inconsistent");
  return GRUB_ERR_NONE;
}

static grub_err_t
udl_boot_callback(void)
{
  void *relocations = 0;
  grub_size_t relocations_size = 0;
  grub_uint32_t cpu_cleanup_mask;
  grub_err_t error;
  if (!udl_active)
    return grub_error(GRUB_ERR_BAD_OS, "direct-loader state is unavailable");
  if (udl_active->boot_failed)
    return grub_error(GRUB_ERR_BAD_OS,
                      "direct-loader handoff already returned an error");
  if (udl_active->diagnostic_stop == UDL_DIAG_STOP_CALLBACK)
    udl_diag_hold(udl_active->diagnostic_stop, UDL_DIAG_STAGE_CALLBACK);
  error = udl_capture_cpu_profile(&cpu_cleanup_mask);
  if (error != GRUB_ERR_NONE)
    {
      udl_active->boot_failed = 1;
      return error;
    }
  if (cpu_cleanup_mask != udl_active->cpu_cleanup_mask)
    {
      udl_active->boot_failed = 1;
      return grub_error(GRUB_ERR_BAD_OS,
                        "direct-loader CPU feature profile changed before handoff");
    }
  if (udl_active->diagnostic_stop == UDL_DIAG_STOP_PROFILE)
    udl_diag_hold(udl_active->diagnostic_stop, UDL_DIAG_STAGE_PROFILE);
  if (udl_active->diagnostic_stop != UDL_DIAG_STOP_PREPARED
      && udl_active->diagnostic_stop != UDL_DIAG_STOP_COPIED)
    grub_printf("direct-loader: handoff-start cleanup-mask=0x%x\n",
                cpu_cleanup_mask);
  if (udl_active->diagnostic_stop == UDL_DIAG_STOP_COPIED)
    udl_active->transition_target = udl_active->raw_gate_target;
  error = grub_relocator_prepare_relocs(udl_active->relocator,
      udl_active->transition_target, &relocations, &relocations_size);
  if (error != GRUB_ERR_NONE)
    {
      udl_active->boot_failed = 1;
      return error;
    }
  if (!relocations || relocations_size == 0
      || relocations_size > ~(grub_addr_t) 0 - (grub_addr_t) relocations)
    {
      udl_active->boot_failed = 1;
      return grub_error(GRUB_ERR_BAD_OS,
                        "generic relocator returned an invalid mover span");
    }
  if (udl_active->diagnostic_stop == UDL_DIAG_STOP_PREPARED)
    udl_diag_hold(udl_active->diagnostic_stop, UDL_DIAG_STAGE_PREPARED);
  if (udl_active->diagnostic_stop == UDL_DIAG_STOP_INVALID)
    grub_printf("direct-loader: generic-movers=%p size=%lu\n",
                relocations, (unsigned long) relocations_size);
  udl_transfer_relocations(relocations);
}

static grub_err_t
udl_unload_callback(void)
{
  struct udl_loader_context *context = udl_active;
  udl_active = 0;
  udl_release_context(context);
  return GRUB_ERR_NONE;
}

static grub_err_t
udl_command_with_stop(grub_command_t command __attribute__((unused)),
                      int argc, char **argv,
                      grub_uint32_t diagnostic_stop)
{
  struct udl_loader_context *context = 0;
  struct udl_map_capture capture;
  udl_memory_map memory_map;
  udl_image kernel_image;
  udl_image hal_image;
  udl_layout layout;
  udl_export_table export_table;
  udl_section *kernel_sections = 0;
  udl_section *hal_sections = 0;
  udl_export *exports = 0;
  char *names = 0;
  udl_write_slot *writes = 0;
  udl_size write_capacity = 1;
  grub_file_t kernel_file = 0;
  grub_file_t hal_file = 0;
  void *kernel_bytes = 0;
  void *hal_bytes = 0;
  grub_size_t kernel_size = 0;
  grub_size_t hal_size = 0;
  char *command_line = 0;
  grub_size_t command_line_length = 0;
  grub_size_t total_span;
  grub_uint32_t kernel_relocations;
  grub_uint32_t hal_relocations;
  grub_uint32_t cpu_cleanup_mask;
  grub_err_t error = GRUB_ERR_NONE;
  udl_error core_error;
  grub_uint8_t *payload;
  udl_boot_info_wire *bootinfo;
  grub_phys_addr_t transition_target = 0;
  grub_phys_addr_t parameter_target = 0;
  grub_size_t transition_size;

  if (argc < 3 || grub_strcmp(argv[2], "--") != 0)
    return grub_error(GRUB_ERR_BAD_ARGUMENT,
                      "usage: undos_direct KERNEL HAL -- [COMMAND-LINE ...]");
  for (int i = 3; i < argc; ++i)
    {
      const grub_size_t token_length = grub_strlen(argv[i]);
      const grub_size_t separator_length = i > 3 ? 1 : 0;
      if (command_line_length > 1023
          || separator_length > 1023 - command_line_length
          || token_length > 1023 - command_line_length - separator_length)
        return grub_error(GRUB_ERR_BAD_ARGUMENT,
                          "direct-loader command line exceeds 1023 bytes");
      command_line_length += separator_length + token_length;
    }

  if (diagnostic_stop != UDL_DIAG_STOP_INVALID)
    {
      error = udl_diag_confirm_video();
      if (error != GRUB_ERR_NONE)
        return error;
      if (diagnostic_stop == UDL_DIAG_STOP_COMMAND)
        udl_diag_hold(diagnostic_stop, UDL_DIAG_STAGE_COMMAND);
    }

  error = udl_capture_cpu_profile(&cpu_cleanup_mask);
  if (error != GRUB_ERR_NONE)
    return error;
  grub_printf("direct-loader: CPU profile cleanup-mask=0x%x\n",
              cpu_cleanup_mask);

  grub_dl_ref(udl_module);
  grub_loader_unset();
  context = grub_zalloc(sizeof(*context));
  if (!context)
    {
      error = grub_errno != GRUB_ERR_NONE
        ? grub_errno : GRUB_ERR_OUT_OF_MEMORY;
      grub_error_push();
      grub_dl_unref(udl_module);
      grub_error_pop();
      return error;
    }
  context->module_reference = 1;
    context->cpu_cleanup_mask = cpu_cleanup_mask;
    context->diagnostic_stop = diagnostic_stop;

  command_line = grub_zalloc(1024);
  if (!command_line)
    {
      error = grub_errno;
      goto fail;
    }
  {
    grub_size_t at = 0;
    for (int i = 3; i < argc; ++i)
      {
        grub_size_t token_length;
        if (i > 3)
          command_line[at++] = ' ';
        token_length = grub_strlen(argv[i]);
        grub_memcpy(command_line + at, argv[i], token_length);
        at += token_length;
      }
      command_line[at] = 0;
  }

  error = udl_read_image(argv[0], GRUB_FILE_TYPE_MULTIBOOT_KERNEL,
                         "kernel", &kernel_file, &kernel_bytes, &kernel_size);
  if (error != GRUB_ERR_NONE)
    goto fail;
  error = udl_read_image(argv[1], GRUB_FILE_TYPE_MULTIBOOT_MODULE,
                         "HAL", &hal_file, &hal_bytes, &hal_size);
  if (error != GRUB_ERR_NONE)
    goto fail;
  error = udl_close_file(&kernel_file, "kernel");
  if (error != GRUB_ERR_NONE)
    goto fail;
  error = udl_close_file(&hal_file, "HAL");
  if (error != GRUB_ERR_NONE)
    goto fail;

  kernel_sections = grub_zalloc(UDL_MAX_SECTIONS * sizeof(udl_section));
  hal_sections = grub_zalloc(UDL_MAX_SECTIONS * sizeof(udl_section));
  if (!kernel_sections || !hal_sections)
    {
      error = grub_errno;
      goto fail;
    }
  core_error = udl_image_open(&kernel_image,
      (udl_blob) { kernel_bytes, kernel_size }, kernel_sections,
      UDL_MAX_SECTIONS, UDL_ROLE_KERNEL);
  if (core_error != UDL_OK)
    {
      error = grub_error(GRUB_ERR_BAD_OS, "kernel ELF: %s",
                         udl_error_name(core_error));
      goto fail;
    }
  core_error = udl_image_open(&hal_image, (udl_blob) { hal_bytes, hal_size },
      hal_sections, UDL_MAX_SECTIONS, UDL_ROLE_HAL);
  if (core_error != UDL_OK)
    {
      error = grub_error(GRUB_ERR_BAD_OS, "HAL ELF: %s",
                         udl_error_name(core_error));
      goto fail;
    }
  core_error = udl_plan_layout(&kernel_image, &hal_image,
      sizeof(udl_boot_info_wire), &layout);
  if (core_error != UDL_OK)
    {
      error = grub_error(GRUB_ERR_BAD_OS, "direct-loader layout: %s",
                         udl_error_name(core_error));
      goto fail;
    }
  exports = grub_zalloc(UDL_MAX_EXPORTS * sizeof(udl_export));
  names = grub_zalloc(UDL_NAME_POOL_SIZE);
  if (!exports || !names)
    {
      error = grub_errno;
      goto fail;
    }
  export_table = (udl_export_table) {
    exports, UDL_MAX_EXPORTS, 0, names, UDL_NAME_POOL_SIZE, 0, 0, 0
  };
  core_error = udl_collect_exports(&kernel_image, &hal_image, &export_table);
  if (core_error != UDL_OK)
    {
      error = grub_error(GRUB_ERR_BAD_OS, "direct-loader exports: %s",
                         udl_error_name(core_error));
      goto fail;
    }
  core_error = udl_image_relocation_count(&kernel_image, &kernel_relocations);
  if (core_error == UDL_OK)
    core_error = udl_image_relocation_count(&hal_image, &hal_relocations);
  if (core_error != UDL_OK)
    {
      error = grub_error(GRUB_ERR_BAD_OS, "direct-loader relocations: %s",
                         udl_error_name(core_error));
      goto fail;
    }
  {
    grub_uint32_t max_relocations = kernel_relocations > hal_relocations
      ? kernel_relocations : hal_relocations;
    grub_uint32_t required = max_relocations ? max_relocations * 2 : 1;
    while (write_capacity < required)
      write_capacity <<= 1;
    writes = grub_zalloc(write_capacity * sizeof(udl_write_slot));
    if (!writes)
      {
        error = grub_errno;
        goto fail;
      }
  }

  grub_memset(&capture, 0, sizeof(capture));
  error = grub_mmap_iterate(udl_map_hook, &capture);
  if (error != GRUB_ERR_NONE)
    goto fail;
  if (capture.overflow || capture.count == 0)
    {
      error = grub_error(GRUB_ERR_OUT_OF_RANGE,
                         "GRUB memory map exceeds the direct-loader capacity");
      goto fail;
    }
  core_error = udl_normalize_memory_map(capture.regions, capture.count,
                                        &memory_map);
  if (core_error != UDL_OK)
    {
      error = grub_error(GRUB_ERR_BAD_OS, "GRUB memory map: %s",
                         udl_error_name(core_error));
      goto fail;
    }

  transition_size = (grub_size_t) (udl_transition_end - udl_transition_start);
  if (transition_size == 0 || transition_size > UDL_PAGE_SIZE)
    {
      error = grub_error(GRUB_ERR_BAD_OS,
                         "direct-loader transition blob exceeds one page");
      goto fail;
    }
  total_span = layout.payload_size + UDL_BOOTSTRAP_SIZE;
  if (total_span < layout.payload_size || total_span > 0x20000000u)
    {
      error = grub_error(GRUB_ERR_OUT_OF_RANGE,
                         "direct-loader payload exceeds the managed memory limit");
      goto fail;
    }
  context->relocator = grub_relocator_new();
  if (!context->relocator)
    {
      error = grub_errno;
      goto fail;
    }
  error = udl_allocate_payload(context, &memory_map, total_span);
  if (error != GRUB_ERR_NONE)
    goto fail;
  payload = context->payload_current;
  grub_memset(payload, 0, total_span);
  core_error = udl_link_images(&kernel_image, &hal_image, &export_table,
      payload, kernel_image.span,
      payload + layout.hal_base - UDL_KERNEL_BASE, hal_image.span,
      writes, write_capacity);
  if (core_error != UDL_OK)
    {
      error = grub_error(GRUB_ERR_BAD_OS, "direct-loader link: %s",
                         udl_error_name(core_error));
      goto fail;
    }
  bootinfo = (udl_boot_info_wire *)
    (payload + layout.bootinfo_base - UDL_KERNEL_BASE);
  error = udl_set_bootinfo(bootinfo, &layout, &memory_map, &export_table,
      command_line, context->payload_target,
      context->payload_target + layout.payload_size);
  if (error != GRUB_ERR_NONE)
    goto fail;
  error = udl_build_page_tables(context, &layout,
      context->payload_target + layout.payload_size,
      transition_size, context->cpu_cleanup_mask,
      context->diagnostic_stop,
      &transition_target, &parameter_target);
  if (error != GRUB_ERR_NONE)
    goto fail;
  if ((grub_uint32_t) transition_target
        != (grub_uint32_t) context->payload_target + layout.payload_size
          + 9 * UDL_PAGE_SIZE + UDL_TRANSITION_ENTRY_OFFSET
      || (grub_uint32_t) parameter_target
        != (grub_uint32_t) context->payload_target + layout.payload_size
          + 9 * UDL_PAGE_SIZE + UDL_TRANSITION_PARAMETERS_OFFSET)
    {
      error = grub_error(GRUB_ERR_BUG,
                         "direct-loader transition addresses are inconsistent");
      goto fail;
    }
  context->transition_target = transition_target;
  context->raw_gate_target = context->payload_target + layout.payload_size
    + 9 * UDL_PAGE_SIZE + UDL_TRANSITION_RAW_GATE_OFFSET;
  context->parameter_target = parameter_target;

  grub_free(kernel_bytes);
  kernel_bytes = 0;
  grub_free(hal_bytes);
  hal_bytes = 0;
  grub_free(kernel_sections);
  kernel_sections = 0;
  grub_free(hal_sections);
  hal_sections = 0;
  grub_free(exports);
  exports = 0;
  grub_free(names);
  names = 0;
  grub_free(writes);
  writes = 0;
  grub_free(command_line);
  command_line = 0;
  if (diagnostic_stop >= UDL_DIAG_STOP_CALLBACK)
    {
      error = grub_env_set("udl_diag_prepared", "1");
      if (error != GRUB_ERR_NONE)
        goto fail;
    }
  udl_active = context;
  grub_loader_set(udl_boot_callback, udl_unload_callback, 0);
  if (diagnostic_stop == UDL_DIAG_STOP_LOADED)
    udl_diag_hold(diagnostic_stop, UDL_DIAG_STAGE_LOADED);
  if (diagnostic_stop == UDL_DIAG_STOP_INVALID)
    grub_printf("direct-loader: prepared kernel and HAL\n");
  return GRUB_ERR_NONE;

fail:
  if (error == GRUB_ERR_NONE)
    error = grub_errno != GRUB_ERR_NONE
      ? grub_errno : GRUB_ERR_BAD_OS;
  if (grub_errno == GRUB_ERR_NONE)
    grub_error(error, "direct-loader preparation failed");
  grub_error_push();
  if (kernel_file)
    grub_file_close(kernel_file);
  if (hal_file)
    grub_file_close(hal_file);
  grub_free(kernel_bytes);
  grub_free(hal_bytes);
  grub_free(kernel_sections);
  grub_free(hal_sections);
  grub_free(exports);
  grub_free(names);
  grub_free(writes);
  grub_free(command_line);
  udl_release_context(context);
  grub_error_pop();
  return error;
}

static grub_err_t
udl_command(grub_command_t command, int argc, char **argv)
{
  return udl_command_with_stop(command, argc, argv, UDL_DIAG_STOP_INVALID);
}

static grub_err_t
udl_diag_command(grub_command_t command, int argc, char **argv)
{
  grub_uint32_t stop;
  if (argc < 4)
    return grub_error(GRUB_ERR_BAD_ARGUMENT,
      "usage: undos_direct_diag STOP|kernel KERNEL HAL -- [COMMAND-LINE ...]");
  stop = udl_diag_stop_from_name(argv[0]);
  if (stop == UDL_DIAG_STOP_INVALID)
    return grub_error(GRUB_ERR_BAD_ARGUMENT,
                      "unknown direct-loader diagnostic STOP");
  if (grub_strcmp(argv[3], "--") != 0)
    return grub_error(GRUB_ERR_BAD_ARGUMENT,
      "usage: undos_direct_diag STOP|kernel KERNEL HAL -- [COMMAND-LINE ...]");
  return udl_command_with_stop(command, argc - 1, argv + 1, stop);
}

GRUB_MOD_INIT(undos_direct)
{
  udl_module = mod;
  udl_command_handle = grub_register_command(
      "undos_direct", udl_command, 0,
      "Load the UnDOS kernel and HAL directly");
  udl_diag_command_handle = grub_register_command(
      "undos_direct_diag", udl_diag_command, 0,
      "Run an opt-in VGA handoff or kernel-entry stop diagnostic");
}

GRUB_MOD_FINI(undos_direct)
{
  if (udl_command_handle)
    grub_unregister_command(udl_command_handle);
  if (udl_diag_command_handle)
    grub_unregister_command(udl_diag_command_handle);
  udl_command_handle = 0;
  udl_diag_command_handle = 0;
}
