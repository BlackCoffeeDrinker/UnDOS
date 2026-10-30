#include "loader_core.h"

#define ELF_HEADER_SIZE 52u
#define ELF_PROGRAM_SIZE 32u
#define ELF_SECTION_SIZE 40u
#define ELF_SYMBOL_SIZE 16u
#define ELF_REL_SIZE 8u
#define ELF_RELA_SIZE 12u
#define ELF_MAX_SEGMENTS 4096u
#define UDL_BOOTINFO_SIZE_MAX 0x00100000u
#define UDL_MANAGED_LIMIT 0x20000000ull
#define UDL_LOW_RESERVED_LIMIT 0x00100000ull
#define UDL_GOT_NAME_LIMIT 64u

enum {
  PT_LOAD = 1,
  PT_DYNAMIC = 2,
  PT_INTERP = 3,
  PT_TLS = 7,
  SHT_NULL = 0,
  SHT_SYMTAB = 2,
  SHT_STRTAB = 3,
  SHT_RELA = 4,
  SHT_NOBITS = 8,
  SHT_REL = 9,
  SHT_DYNSYM = 11,
  SHF_ALLOC = 2,
  SHN_UNDEF = 0,
  SHN_ABS = 0xfff1,
  SHN_COMMON = 0xfff2,
  SHN_XINDEX = 0xffff,
  STB_GLOBAL = 1,
  STB_WEAK = 2,
  STV_DEFAULT = 0,
  STV_PROTECTED = 3,
  STT_TLS = 6,
  R_386_NONE = 0,
  R_386_32 = 1,
  R_386_PC32 = 2,
  R_386_GOT32 = 3,
  R_386_PLT32 = 4,
  R_386_RELATIVE = 8,
  R_386_GOTOFF = 9,
  R_386_GOTPC = 10,
  R_386_GOT32X = 43
};

typedef struct {
  udl_u32 type;
  udl_u32 offset;
  udl_u32 address;
  udl_u32 file_size;
  udl_u32 memory_size;
  udl_u32 alignment;
} udl_segment;

typedef struct {
  udl_u64 start;
  udl_u64 end;
  udl_u32 type;
} udl_interval;

static udl_u16
read16(const udl_u8 *p)
{
  return (udl_u16) p[0] | ((udl_u16) p[1] << 8);
}

static udl_u32
read32(const udl_u8 *p)
{
  return (udl_u32) p[0] | ((udl_u32) p[1] << 8)
    | ((udl_u32) p[2] << 16) | ((udl_u32) p[3] << 24);
}

static void
write32(udl_u8 *p, udl_u32 value)
{
  p[0] = (udl_u8) value;
  p[1] = (udl_u8) (value >> 8);
  p[2] = (udl_u8) (value >> 16);
  p[3] = (udl_u8) (value >> 24);
}

static int
range_fits(udl_size size, udl_u32 offset, udl_u32 length)
{
  return (udl_size) offset <= size
    && (udl_size) length <= size - (udl_size) offset;
}

static int
add32(udl_u32 left, udl_u32 right, udl_u32 *out)
{
  const udl_u64 result = (udl_u64) left + right;
  if (result > 0xffffffffull)
    return 0;
  *out = (udl_u32) result;
  return 1;
}

static int
power_of_two(udl_u32 value)
{
  return value != 0 && (value & (value - 1)) == 0;
}

static int
read_section(const udl_image *image, udl_u32 index, udl_section *section)
{
  if (index >= image->section_count)
    return 0;
  *section = image->sections[index];
  return 1;
}

static int
get_symbol(const udl_image *image, udl_u32 index, udl_u32 *name,
           udl_u32 *value, udl_u32 *size, udl_u8 *info, udl_u8 *other,
           udl_u16 *section)
{
  const udl_section *symtab;
  const udl_section *strings;
  const udl_u32 count = image->sections[image->symbol_section].size / ELF_SYMBOL_SIZE;
  const udl_u8 *symbol;

  if (index >= count)
    return 0;
  symtab = &image->sections[image->symbol_section];
  strings = &image->sections[image->string_section];
  symbol = image->blob.bytes + symtab->offset + index * ELF_SYMBOL_SIZE;
  *name = read32(symbol);
  *value = read32(symbol + 4);
  *size = read32(symbol + 8);
  *info = symbol[12];
  *other = symbol[13];
  *section = read16(symbol + 14);
  return *name < strings->size;
}

static const char *
bounded_string(const udl_image *image, udl_u32 section_index,
               udl_u32 offset, udl_u32 *length)
{
  const udl_section *section;
  const udl_u8 *bytes;
  udl_u32 remaining;

  if (section_index >= image->section_count)
    return 0;
  section = &image->sections[section_index];
  if (offset >= section->size)
    return 0;
  bytes = image->blob.bytes + section->offset + offset;
  remaining = section->size - offset;
  for (udl_u32 i = 0; i < remaining; ++i)
    if (bytes[i] == 0)
      {
        *length = i;
        return (const char *) bytes;
      }
  return 0;
}

static int
string_equal(const char *left, const char *right)
{
  while (*left && *left == *right)
    {
      ++left;
      ++right;
    }
  return *left == *right;
}

static int
string_prefix(const char *value, const char *prefix)
{
  while (*prefix)
    if (*value++ != *prefix++)
      return 0;
  return 1;
}

static int
range_in_segment(const udl_image *image, udl_u32 address, udl_u32 length)
{
  const udl_u32 phoff = image->program_offset;
  for (udl_u32 i = 0; i < image->program_count; ++i)
    {
      const udl_u8 *program = image->blob.bytes + phoff + i * ELF_PROGRAM_SIZE;
      udl_u32 end;
      const udl_u32 start = read32(program + 8);
      const udl_u32 memsz = read32(program + 20);
      if (read32(program) != PT_LOAD || !memsz
          || !add32(start, memsz, &end))
        continue;
      if (address >= start && address <= end && length <= end - address)
        return 1;
    }
  return 0;
}

static int
section_is_loaded(const udl_image *image, const udl_section *section)
{
  return (section->flags & SHF_ALLOC) == 0
    || (section->size == 0
        || range_in_segment(image, section->address, section->size));
}

static udl_u32
segment_byte(const udl_image *image, const udl_segment *segment, udl_u32 address)
{
  const udl_u32 offset = address - segment->address;
  return offset < segment->file_size
    ? image->blob.bytes[segment->offset + offset] : 0;
}

static int
read_segment(const udl_image *image, udl_u32 index, udl_segment *segment)
{
  const udl_u8 *program;
  if (index >= image->program_count)
    return 0;
  program = image->blob.bytes + image->program_offset
    + index * ELF_PROGRAM_SIZE;
  if (read32(program) != PT_LOAD)
    return 0;
  segment->type = PT_LOAD;
  segment->offset = read32(program + 4);
  segment->address = read32(program + 8);
  segment->file_size = read32(program + 16);
  segment->memory_size = read32(program + 20);
  segment->alignment = read32(program + 28);
  return segment->memory_size != 0;
}

static udl_error
check_segment_overlaps(const udl_image *image)
{
  for (udl_u32 i = 0; i < image->program_count; ++i)
    {
      udl_segment left;
      if (!read_segment(image, i, &left))
        continue;
      for (udl_u32 j = i + 1; j < image->program_count; ++j)
      {
        udl_segment right;
        udl_u32 a_end;
        udl_u32 b_end;
        udl_u32 start;
        udl_u32 end;
        if (!read_segment(image, j, &right))
          continue;
        a_end = left.address + left.memory_size;
        b_end = right.address + right.memory_size;
        start = left.address > right.address ? left.address : right.address;
        end = a_end < b_end ? a_end : b_end;
        if (start >= end)
          continue;
        for (udl_u32 at = start; at < end; ++at)
          if (segment_byte(image, &left, at)
              != segment_byte(image, &right, at))
            return UDL_BAD_ELF;
      }
    }
  return UDL_OK;
}

static int
validate_strings(udl_image *image)
{
  const udl_section *names;
  udl_u32 ignored;
  if (image->section_count == 0)
    return 0;
  names = &image->sections[read16(image->blob.bytes + 50)];
  if (names->type != SHT_STRTAB || names->size == 0)
    return 0;
  for (udl_u32 i = 0; i < image->section_count; ++i)
    if (!bounded_string(image, read16(image->blob.bytes + 50),
                        image->sections[i].name, &ignored))
      return 0;
  return 1;
}

udl_error
udl_image_open(udl_image *image, udl_blob blob, udl_section *sections,
               udl_size section_capacity, udl_u8 role)
{
  const udl_u8 *header;
  udl_u32 phoff;
  udl_u32 shoff;
  udl_u32 min_address = 0xffffffffu;
  udl_u32 max_address = 0;
  udl_u32 segment_count = 0;
  udl_u16 phnum;
  udl_u16 shnum;
  udl_u16 shstrndx;
  udl_u32 expected_base;

  if (!image || !blob.bytes || !sections || role > UDL_ROLE_HAL)
    return UDL_BAD_ARGUMENT;
  if (blob.size < ELF_HEADER_SIZE)
    return UDL_BAD_ELF;
  if (blob.size > 0xffffffffu)
    return UDL_CAPACITY;
  header = blob.bytes;
  if (header[0] != 0x7f || header[1] != 'E' || header[2] != 'L'
      || header[3] != 'F' || header[4] != 1 || header[5] != 1
      || header[6] != 1 || read16(header + 16) != 2
      || read16(header + 18) != 3 || read32(header + 20) != 1)
    return UDL_UNSUPPORTED_ELF;
  phoff = read32(header + 28);
  shoff = read32(header + 32);
  phnum = read16(header + 44);
  shnum = read16(header + 48);
  shstrndx = read16(header + 50);
  if (read16(header + 40) != ELF_HEADER_SIZE
      || read16(header + 42) != ELF_PROGRAM_SIZE
      || read16(header + 46) != ELF_SECTION_SIZE
      || phnum == 0 || phnum > ELF_MAX_SEGMENTS || phnum == 0xffffu
      || shnum == 0 || shnum == 0xffffu
      || shstrndx == 0xffffu || shstrndx >= shnum)
    return UDL_BAD_ELF;
  if (shnum > UDL_MAX_SECTIONS || shnum > section_capacity
      || !range_fits(blob.size, phoff, (udl_u32) phnum * ELF_PROGRAM_SIZE)
      || !range_fits(blob.size, shoff, (udl_u32) shnum * ELF_SECTION_SIZE))
    return UDL_CAPACITY;

  image->blob = blob;
  image->sections = sections;
  image->section_capacity = section_capacity;
  image->role = role;
  image->linked_base = role == UDL_ROLE_KERNEL
    ? UDL_KERNEL_BASE : UDL_HAL_LINK_BASE;
  image->runtime_base = 0;
  image->end = 0;
  image->span = 0;
  image->entry = read32(header + 24);
  image->program_offset = phoff;
  image->program_count = phnum;
  image->section_count = shnum;
  image->symbol_section = 0xffffffffu;
  image->string_section = 0xffffffffu;
  image->got_section = 0xffffffffu;
  image->got_linked_base = 0;
  image->got_size = 0;
  image->bias = 0;
  expected_base = image->linked_base;

  for (udl_u32 i = 0; i < shnum; ++i)
    {
      const udl_u8 *source = blob.bytes + shoff + i * ELF_SECTION_SIZE;
      udl_section *section = &sections[i];
      section->name = read32(source);
      section->type = read32(source + 4);
      section->flags = read32(source + 8);
      section->address = read32(source + 12);
      section->offset = read32(source + 16);
      section->size = read32(source + 20);
      section->link = read32(source + 24);
      section->info = read32(source + 28);
      section->alignment = read32(source + 32);
      section->entry_size = read32(source + 36);
      if (section->alignment > 1 && !power_of_two(section->alignment))
        return UDL_BAD_ELF;
      if (section->type != SHT_NULL && section->type != SHT_NOBITS
          && !range_fits(blob.size, section->offset, section->size))
        return UDL_BAD_ELF;
      if (section->type == SHT_SYMTAB)
        {
          if (image->symbol_section != 0xffffffffu)
            return UDL_UNSUPPORTED_ELF;
          image->symbol_section = i;
        }
      if (section->type == SHT_DYNSYM)
        return UDL_UNSUPPORTED_ELF;
      if (section->type == SHT_RELA && (section->flags & SHF_ALLOC))
        return UDL_UNSUPPORTED_ELF;
      if (section->type == SHT_RELA && section->info >= shnum)
        return UDL_BAD_ELF;
      if (section->type == SHT_REL && section->info >= shnum)
        return UDL_BAD_ELF;
      if (section->flags & SHF_ALLOC)
        {
          udl_u32 ignored;
          if (!add32(section->address, section->size, &ignored))
            return UDL_BAD_ELF;
        }
    }
  if (!validate_strings(image))
    return UDL_BAD_ELF;
  if (image->symbol_section == 0xffffffffu)
    return UDL_BAD_ELF;
  {
    udl_section *symtab = &sections[image->symbol_section];
    if (symtab->entry_size != ELF_SYMBOL_SIZE
        || symtab->size % ELF_SYMBOL_SIZE != 0
        || symtab->size / ELF_SYMBOL_SIZE > UDL_MAX_SYMBOLS
        || symtab->link >= shnum
        || sections[symtab->link].type != SHT_STRTAB
        || !section_is_loaded(image, symtab)
        || !section_is_loaded(image, &sections[symtab->link]))
      return UDL_BAD_ELF;
    image->string_section = symtab->link;
  }

  for (udl_u32 i = 0; i < phnum; ++i)
    {
      const udl_u8 *program = blob.bytes + phoff + i * ELF_PROGRAM_SIZE;
      const udl_u32 type = read32(program);
      if (type == PT_INTERP || type == PT_DYNAMIC || type == PT_TLS)
        return UDL_UNSUPPORTED_ELF;
      if (type != PT_LOAD)
        continue;
      udl_segment segment;
      udl_u32 end;
      if (segment_count >= ELF_MAX_SEGMENTS)
        return UDL_CAPACITY;
      segment.type = type;
      segment.offset = read32(program + 4);
      segment.address = read32(program + 8);
      segment.file_size = read32(program + 16);
      segment.memory_size = read32(program + 20);
      segment.alignment = read32(program + 28);
      if (segment.file_size > segment.memory_size
          || !range_fits(blob.size, segment.offset, segment.file_size)
          || (segment.alignment > 1
              && (!power_of_two(segment.alignment)
                  || segment.offset % segment.alignment
                    != segment.address % segment.alignment))
          || !add32(segment.address, segment.memory_size, &end))
        return UDL_BAD_ELF;
      if (segment.memory_size == 0)
        continue;
      if (segment.address < min_address)
        min_address = segment.address;
      if (end > max_address)
        max_address = end;
      ++segment_count;
    }
  if (segment_count == 0 || min_address != expected_base
      || max_address < min_address || max_address > UDL_VIRTUAL_LIMIT
      || max_address - min_address > UDL_VIRTUAL_LIMIT - UDL_KERNEL_BASE)
    return UDL_BAD_LAYOUT;
  if (check_segment_overlaps(image) != UDL_OK)
    return UDL_BAD_ELF;
  image->end = max_address;
  image->span = max_address - min_address;
  if (image->span == 0
      || (role == UDL_ROLE_KERNEL
          && (image->entry < min_address || image->entry >= max_address)))
    return UDL_BAD_LAYOUT;
  if (role == UDL_ROLE_KERNEL)
    {
    int executable_entry = 0;
    for (udl_u32 i = 0; i < phnum; ++i)
      {
        const udl_u8 *program = blob.bytes + phoff + i * ELF_PROGRAM_SIZE;
        const udl_u32 start = read32(program + 8);
        const udl_u32 end = start + read32(program + 20);
        if (read32(program) == PT_LOAD && (read32(program + 24) & 1u)
            && image->entry >= start && image->entry < end)
          executable_entry = 1;
      }
    if (!executable_entry)
      return UDL_BAD_LAYOUT;
  }

  for (udl_u32 i = 0; i < shnum; ++i)
    if (!section_is_loaded(image, &sections[i]))
      return UDL_BAD_ELF;

  {
    const udl_section *symtab = &sections[image->symbol_section];
    const udl_section *strtab = &sections[image->string_section];
    const udl_u32 symbol_count = symtab->size / ELF_SYMBOL_SIZE;
    for (udl_u32 i = 0; i < symbol_count; ++i)
      {
        const udl_u8 *symbol = blob.bytes + symtab->offset + i * ELF_SYMBOL_SIZE;
        const udl_u32 name_offset = read32(symbol);
        const udl_u8 type = symbol[12] & 0x0fu;
        const udl_u16 section_index = read16(symbol + 14);
        udl_u32 name_length;
        const udl_u32 section_value = read32(symbol + 4);
        const udl_u32 symbol_size = read32(symbol + 8);
        if (name_offset >= strtab->size
            || !bounded_string(image, image->string_section,
                               name_offset, &name_length))
          return UDL_BAD_ELF;
        if (type == STT_TLS || section_index == SHN_COMMON
            || section_index == SHN_XINDEX
            || (section_index >= shnum && section_index != SHN_UNDEF
                && section_index != SHN_ABS))
          return UDL_UNSUPPORTED_ELF;
        if (section_index < shnum && section_index != SHN_UNDEF
            && (sections[section_index].flags & SHF_ALLOC)
            && symbol_size != 0
            && !range_in_segment(image, section_value, symbol_size))
          return UDL_BAD_ELF;
      }
  }

  {
    udl_u32 relocation_count = 0;
    for (udl_u32 i = 0; i < shnum; ++i)
      {
        const udl_section *relocations = &sections[i];
        const udl_section *target;
        const udl_u32 entry_size = relocations->type == SHT_REL
          ? ELF_REL_SIZE : ELF_RELA_SIZE;
        const udl_u32 count = relocations->size / entry_size;
        if (relocations->type != SHT_REL && relocations->type != SHT_RELA)
          continue;
        if (relocations->entry_size != entry_size
            || relocations->size % entry_size != 0
            || relocations->link != image->symbol_section
            || relocations->info >= shnum)
          return UDL_BAD_ELF;
        if (count > UDL_MAX_RELOCATIONS - relocation_count)
          return UDL_CAPACITY;
        relocation_count += count;
        target = &sections[relocations->info];
        for (udl_u32 r = 0; r < count; ++r)
          {
            const udl_u8 *rel = blob.bytes + relocations->offset + r * entry_size;
            const udl_u32 info = read32(rel + 4);
            const udl_u32 symbol_index = info >> 8;
            const udl_u32 rel_type = info & 0xffu;
            const udl_u32 site = read32(rel);
            if (symbol_index >= sections[image->symbol_section].size / ELF_SYMBOL_SIZE)
              return UDL_BAD_ELF;
            if ((target->flags & SHF_ALLOC) && rel_type != R_386_NONE
                && (site < target->address
                    || site - target->address > target->size
                    || target->size - (site - target->address) < 4
                    || !range_in_segment(image, site, 4)))
              return UDL_BAD_ELF;
          }
        if (relocations->type == SHT_RELA
            && (target->flags & SHF_ALLOC))
          return UDL_UNSUPPORTED_ELF;
      }
  }

  {
    const udl_section *symtab = &sections[image->symbol_section];
    const udl_u32 count = symtab->size / ELF_SYMBOL_SIZE;
    for (udl_u32 i = 0; i < count; ++i)
      {
        udl_u32 name, value, size;
        udl_u8 info, other;
        udl_u16 section_index;
        udl_u32 name_length;
        const char *symbol_name;
        udl_section section;
        if (!get_symbol(image, i, &name, &value, &size, &info, &other,
                        &section_index))
          return UDL_BAD_ELF;
        symbol_name = bounded_string(image, image->string_section, name,
                                     &name_length);
        if (!symbol_name)
          return UDL_BAD_ELF;
        if (name_length == sizeof("_GLOBAL_OFFSET_TABLE_") - 1
            && string_equal(symbol_name, "_GLOBAL_OFFSET_TABLE_"))
          {
            if (section_index >= shnum
                || !read_section(image, section_index, &section)
                || !(section.flags & SHF_ALLOC)
                    || !string_prefix(bounded_string(
                          image, shstrndx, section.name, &name_length), ".got")
                || value < section.address
                || value - section.address >= section.size)
              return UDL_BAD_ELF;
            image->got_section = section_index;
            image->got_linked_base = value;
            image->got_size = section.address + section.size - value;
          }
      }
  }
  return UDL_OK;
}

udl_error
udl_plan_layout(udl_image *kernel, udl_image *hal, udl_u32 bootinfo_size,
                udl_layout *layout)
{
  udl_u32 cursor;
  if (!kernel || !hal || !layout || kernel->role != UDL_ROLE_KERNEL
      || hal->role != UDL_ROLE_HAL || bootinfo_size == 0
      || bootinfo_size > UDL_BOOTINFO_SIZE_MAX)
    return UDL_BAD_ARGUMENT;
  if (kernel->span > UDL_VIRTUAL_LIMIT - UDL_KERNEL_BASE)
    return UDL_BAD_LAYOUT;
  kernel->runtime_base = UDL_KERNEL_BASE;
  kernel->bias = kernel->runtime_base - kernel->linked_base;
  if (!add32(kernel->runtime_base, kernel->span, &cursor))
    return UDL_BAD_LAYOUT;
  cursor = (cursor + (UDL_PAGE_SIZE - 1u)) & ~(UDL_PAGE_SIZE - 1u);
  if (cursor < UDL_KERNEL_BASE || cursor > UDL_VIRTUAL_LIMIT)
    return UDL_BAD_LAYOUT;
  hal->runtime_base = cursor;
  hal->bias = hal->runtime_base - hal->linked_base;
  if (hal->span > UDL_VIRTUAL_LIMIT - hal->runtime_base)
    return UDL_BAD_LAYOUT;
  if (!add32(hal->runtime_base, hal->span, &cursor))
    return UDL_BAD_LAYOUT;
  cursor = (cursor + (UDL_PAGE_SIZE - 1u)) & ~(UDL_PAGE_SIZE - 1u);
  if (cursor > UDL_VIRTUAL_LIMIT)
    return UDL_BAD_LAYOUT;

  layout->kernel = *kernel;
  layout->hal = *hal;
  layout->kernel_end = (kernel->runtime_base + kernel->span
                        + UDL_PAGE_SIZE - 1u) & ~(UDL_PAGE_SIZE - 1u);
  layout->hal_base = hal->runtime_base;
  layout->hal_end = cursor;
  layout->stack_bottom = cursor;
  if (!add32(cursor, 16384u, &layout->stack_top))
    return UDL_BAD_LAYOUT;
  layout->bootinfo_base = layout->stack_top;
  if (!add32(layout->bootinfo_base, bootinfo_size, &cursor))
    return UDL_BAD_LAYOUT;
  layout->bootinfo_end = (cursor + UDL_PAGE_SIZE - 1u)
    & ~(UDL_PAGE_SIZE - 1u);
  if (layout->bootinfo_end < cursor
      || layout->bootinfo_end > UDL_VIRTUAL_LIMIT)
    return UDL_BAD_LAYOUT;
  layout->payload_size = layout->bootinfo_end - UDL_KERNEL_BASE;
  layout->kernel_entry = kernel->entry + kernel->bias;
  return UDL_OK;
}

static int
export_by_name(const udl_export_table *table, const char *name,
               udl_u32 *address)
{
  for (udl_size i = 0; i < table->export_count; ++i)
    if (string_equal(table->exports[i].name, name))
      {
        *address = table->exports[i].address;
        return 1;
      }
  return 0;
}

udl_error
udl_collect_exports(udl_image *kernel, udl_image *hal,
                    udl_export_table *table)
{
  udl_image *images[2] = { kernel, hal };
  if (!kernel || !hal || !table || !table->exports || !table->name_pool
      || table->export_capacity < UDL_MAX_EXPORTS
      || table->name_capacity < UDL_NAME_POOL_SIZE)
    return UDL_BAD_ARGUMENT;
  table->export_count = 0;
  table->names_used = 0;
  table->kernel_export_count = 0;
  table->duplicate_exports = 0;
  for (udl_u32 image_index = 0; image_index < 2; ++image_index)
    {
      udl_image *image = images[image_index];
      const udl_section *symtab = &image->sections[image->symbol_section];
      const udl_u32 count = symtab->size / ELF_SYMBOL_SIZE;
      for (udl_u32 i = 0; i < count; ++i)
        {
          udl_u32 name_offset, value, size, name_length;
          udl_u8 info, other;
          udl_u16 section_index;
          const char *name;
          udl_u32 runtime_value;
          if (!get_symbol(image, i, &name_offset, &value, &size, &info,
                          &other, &section_index))
            return UDL_BAD_ELF;
          name = bounded_string(image, image->string_section, name_offset,
                                &name_length);
          if (!name)
            return UDL_BAD_ELF;
          if (!name_length
              || ((info >> 4) != STB_GLOBAL && (info >> 4) != STB_WEAK)
              || ((other & 3u) != STV_DEFAULT
                  && (other & 3u) != STV_PROTECTED)
              || section_index == SHN_UNDEF)
            continue;
          if (section_index == SHN_ABS)
            runtime_value = value;
          else
            {
              udl_section *section;
              if (section_index >= image->section_count)
                return UDL_UNSUPPORTED_ELF;
              section = &image->sections[section_index];
              if (!(section->flags & SHF_ALLOC))
                continue;
              if (value < image->linked_base
                  || value - image->linked_base >= image->span
                  || !add32(value, image->bias, &runtime_value))
                return UDL_BAD_ELF;
            }
          {
            udl_u32 existing;
            if (export_by_name(table, name, &existing))
              {
                ++table->duplicate_exports;
                continue;
              }
          }
          if (table->export_count >= table->export_capacity
              || name_length + 1 > table->name_capacity - table->names_used)
            return UDL_CAPACITY;
          {
            const udl_size name_at = table->names_used;
            for (udl_u32 c = 0; c < name_length; ++c)
              table->name_pool[name_at + c] = name[c];
            table->name_pool[name_at + name_length] = 0;
            table->names_used += name_length + 1;
            table->exports[table->export_count].name =
              table->name_pool + name_at;
            table->exports[table->export_count].address = runtime_value;
            table->exports[table->export_count].name_offset = name_at;
            table->exports[table->export_count].image = image_index;
            ++table->export_count;
          }
        }
      if (image_index == 0)
        table->kernel_export_count = table->export_count;
    }
  return UDL_OK;
}

static int
section_contains(const udl_image *image, udl_u32 section_index,
                 udl_u32 address, udl_u32 length)
{
  const udl_section *section;
  if (section_index >= image->section_count)
    return 0;
  section = &image->sections[section_index];
  return (section->flags & SHF_ALLOC) && address >= section->address
    && address - section->address <= section->size
    && length <= section->size - (address - section->address);
}

static int
loaded_word_offset(const udl_image *image, udl_u32 address, udl_u32 *offset)
{
  udl_u32 runtime;
  if (!add32(address, image->bias, &runtime))
    return 0;
  if (runtime < image->runtime_base
      || runtime - image->runtime_base > image->span
      || image->span - (runtime - image->runtime_base) < 4)
    return 0;
  *offset = runtime - image->runtime_base;
  return 1;
}

static udl_error
register_write(udl_write_slot *slots, udl_size capacity,
               udl_u32 site, udl_u32 value)
{
  udl_size position;
  if (!slots || capacity == 0 || (capacity & (capacity - 1)) != 0)
    return UDL_CAPACITY;
  position = ((udl_size) (site * 2654435761u)) & (capacity - 1);
  for (udl_size i = 0; i < capacity; ++i)
    {
      udl_write_slot *slot = &slots[position];
      if (slot->site == 0xffffffffu)
        {
          slot->site = site;
          slot->value = value;
          return UDL_OK;
        }
      if (slot->site == site)
        return slot->value == value ? UDL_OK : UDL_BAD_RELOCATION;
      position = (position + 1) & (capacity - 1);
    }
  return UDL_CAPACITY;
}

udl_error
udl_image_relocation_count(const udl_image *image,
                           udl_u32 *relocation_count)
{
  udl_u32 count = 0;
  if (!image || !relocation_count)
    return UDL_BAD_ARGUMENT;
  for (udl_u32 i = 0; i < image->section_count; ++i)
    if (image->sections[i].type == SHT_REL)
      {
        const udl_u32 entries = image->sections[i].size / ELF_REL_SIZE;
        if (entries > UDL_MAX_RELOCATIONS - count)
          return UDL_CAPACITY;
        count += entries;
      }
  *relocation_count = count;
  return UDL_OK;
}

static udl_error
relocate_one(udl_image *image, udl_u32 target_section,
             udl_export_table *exports, udl_u8 *destination,
             udl_u32 rel_offset, udl_u32 info, udl_write_slot *writes,
             udl_size write_capacity)
{
  const udl_u32 symbol_index = info >> 8;
  const udl_u32 type = info & 0xffu;
  udl_u32 name_offset, symbol_value, symbol_size, patch_offset;
  udl_u8 symbol_info, symbol_other;
  udl_u16 symbol_section;
  udl_u32 delta_symbol = 0;
  udl_u32 patch_address;
  udl_u32 linked_word;
  udl_u32 output_value;
  udl_error error;
  const udl_section *symtab = &image->sections[image->symbol_section];
  const udl_u32 symbol_count = symtab->size / ELF_SYMBOL_SIZE;
  const char *symbol_name;
  udl_u32 name_length;
  int same_image_defined = 0;

  if (type == R_386_NONE)
    return UDL_OK;
  if (symbol_index >= symbol_count || !loaded_word_offset(image, rel_offset,
                                                           &patch_offset)
      || !section_contains(image, target_section, rel_offset, 4)
      || !add32(rel_offset, image->bias, &patch_address))
    return UDL_BAD_RELOCATION;
  if (!get_symbol(image, symbol_index, &name_offset, &symbol_value,
                  &symbol_size, &symbol_info, &symbol_other, &symbol_section))
    return UDL_BAD_ELF;
  symbol_name = bounded_string(image, image->string_section, name_offset,
                               &name_length);
  if (!symbol_name)
    return UDL_BAD_ELF;
  linked_word = read32(destination + patch_offset);
  if (type == R_386_RELATIVE)
    {
      if (symbol_index != 0)
        return UDL_BAD_RELOCATION;
    }
  else if (symbol_section == SHN_ABS)
    delta_symbol = 0;
  else if (symbol_section != SHN_UNDEF)
    {
      if (symbol_section >= image->section_count)
        return UDL_UNSUPPORTED_ELF;
      if (!(image->sections[symbol_section].flags & SHF_ALLOC))
        return UDL_BAD_RELOCATION;
      if (symbol_value < image->sections[symbol_section].address
          || symbol_value - image->sections[symbol_section].address
            > image->sections[symbol_section].size
          || symbol_size > image->sections[symbol_section].size
            - (symbol_value - image->sections[symbol_section].address)
          || !add32(symbol_value, image->bias, &delta_symbol))
        return UDL_BAD_RELOCATION;
      same_image_defined = 1;
      delta_symbol = image->bias;
    }
  else if (symbol_index != 0)
    {
      udl_u32 resolved;
      if (!export_by_name(exports, symbol_name, &resolved))
        return UDL_UNRESOLVED_SYMBOL;
      delta_symbol = resolved;
    }

  switch (type)
    {
    case R_386_32:
      output_value = linked_word + delta_symbol;
      break;
    case R_386_PC32:
    case R_386_PLT32:
    case R_386_GOTOFF:
      output_value = linked_word + delta_symbol - image->bias;
      break;
    case R_386_RELATIVE:
      output_value = linked_word + image->bias;
      break;
    case R_386_GOTPC:
      if (image->got_section == 0xffffffffu
          || !section_contains(image, image->got_section,
                               image->got_linked_base, 1)
          || !section_contains(image, target_section, rel_offset, 4)
          || !string_equal(symbol_name, "_GLOBAL_OFFSET_TABLE_"))
        {
          return UDL_BAD_RELOCATION;
        }
      return UDL_OK;
    case R_386_GOT32:
    case R_386_GOT32X:
      {
        const udl_u32 got_offset = linked_word;
        udl_u32 got_site;
        udl_u32 got_runtime;
        udl_u32 got_value;
        if (image->got_section == 0xffffffffu)
          return UDL_BAD_RELOCATION;
        {
          const __INT32_TYPE__ signed_offset = (__INT32_TYPE__) got_offset;
          if (signed_offset < 0)
            {
              const udl_u32 magnitude = (udl_u32) (-(signed_offset + 1)) + 1;
              if (magnitude > image->got_linked_base)
                return UDL_BAD_RELOCATION;
              got_site = image->got_linked_base - magnitude;
            }
          else if (!add32(image->got_linked_base,
                          (udl_u32) signed_offset, &got_site))
            return UDL_BAD_RELOCATION;
        }
        if (!section_contains(image, image->got_section, got_site, 4)
            || !range_in_segment(image, got_site, 4)
            || !loaded_word_offset(image, got_site, &got_runtime))
          return UDL_BAD_RELOCATION;
        if (symbol_index == 0)
          return UDL_BAD_RELOCATION;
        if (symbol_section == SHN_ABS)
          got_value = symbol_value;
        else if (same_image_defined)
          {
            if (!add32(symbol_value, image->bias, &got_value))
              return UDL_BAD_RELOCATION;
          }
        else if (symbol_section == SHN_UNDEF)
          {
            if (!export_by_name(exports, symbol_name, &got_value))
              return UDL_UNRESOLVED_SYMBOL;
          }
        else
          return UDL_BAD_RELOCATION;
        {
          udl_u32 got_runtime_address;
          if (!add32(image->runtime_base, got_runtime, &got_runtime_address))
            return UDL_BAD_RELOCATION;
          error = register_write(writes, write_capacity,
                                 got_runtime_address, got_value);
        }
        if (error != UDL_OK)
          return error;
        write32(destination + got_runtime, got_value);
        return UDL_OK;
      }
    default:
      return UDL_BAD_RELOCATION;
    }

  error = register_write(writes, write_capacity, patch_address, output_value);
  if (error != UDL_OK)
    return error;
  write32(destination + patch_offset, output_value);
  return UDL_OK;
}

static udl_error
link_one(udl_image *image, udl_export_table *exports,
         udl_u8 *destination, udl_write_slot *writes,
         udl_size write_capacity)
{
  for (udl_u32 i = 0; i < image->section_count; ++i)
    {
      const udl_section *relocations = &image->sections[i];
      const udl_section *target;
      const udl_u32 count = relocations->size / ELF_REL_SIZE;
      if (relocations->type != SHT_REL)
        continue;
      target = &image->sections[relocations->info];
      if (!(target->flags & SHF_ALLOC))
        continue;
      for (udl_u32 r = 0; r < count; ++r)
        {
          const udl_u8 *rel = image->blob.bytes + relocations->offset
            + r * ELF_REL_SIZE;
          const udl_error error = relocate_one(image, relocations->info,
              exports, destination, read32(rel), read32(rel + 4), writes,
              write_capacity);
          if (error != UDL_OK)
            return error;
        }
    }
  return UDL_OK;
}

udl_error
udl_link_images(udl_image *kernel, udl_image *hal, udl_export_table *exports,
                udl_u8 *kernel_destination, udl_size kernel_size,
                udl_u8 *hal_destination, udl_size hal_size,
                udl_write_slot *writes, udl_size write_capacity)
{
  udl_image *images[2] = { kernel, hal };
  udl_u8 *destinations[2] = { kernel_destination, hal_destination };
  const udl_size sizes[2] = { kernel_size, hal_size };
  if (!kernel || !hal || !exports || !kernel_destination || !hal_destination
      || kernel_size < kernel->span || hal_size < hal->span
      || !writes || write_capacity == 0)
    return UDL_BAD_ARGUMENT;
  for (udl_size i = 0; i < write_capacity; ++i)
    writes[i].site = 0xffffffffu;
  for (udl_size i = 0; i < 2; ++i)
    {
      udl_image *image = images[i];
      const udl_u32 phoff = image->program_offset;
      if (sizes[i] < image->span)
        return UDL_BAD_ARGUMENT;
      for (udl_size b = 0; b < image->span; ++b)
        destinations[i][b] = 0;
      for (udl_u32 p = 0; p < image->program_count; ++p)
        {
          const udl_u8 *program = image->blob.bytes + phoff
            + p * ELF_PROGRAM_SIZE;
          const udl_u32 type = read32(program);
          const udl_u32 offset = read32(program + 4);
          const udl_u32 address = read32(program + 8);
          const udl_u32 file_size = read32(program + 16);
          const udl_u32 memory_size = read32(program + 20);
          if (type != PT_LOAD || memory_size == 0)
            continue;
          if (address < image->linked_base
              || address - image->linked_base > image->span
              || memory_size > image->span - (address - image->linked_base))
            return UDL_BAD_LAYOUT;
          for (udl_u32 b = 0; b < file_size; ++b)
            destinations[i][address - image->linked_base + b]
              = image->blob.bytes[offset + b];
        }
    }
  {
    udl_error error = link_one(kernel, exports, kernel_destination,
                               writes, write_capacity);
    if (error != UDL_OK)
      return error;
    for (udl_size i = 0; i < write_capacity; ++i)
      writes[i].site = 0xffffffffu;
    error = link_one(hal, exports, hal_destination,
                     writes, write_capacity);
    if (error != UDL_OK)
      return error;
  }
  return UDL_OK;
}

static udl_u64
round_down(udl_u64 value)
{
  return value & ~(udl_u64) (UDL_PAGE_SIZE - 1u);
}

static udl_u64
round_up(udl_u64 value)
{
  return (value + UDL_PAGE_SIZE - 1u)
    & ~(udl_u64) (UDL_PAGE_SIZE - 1u);
}

static int
interval_priority(udl_u32 type)
{
  if (type == UDL_MEMORY_RESERVED)
    return 3;
  if (type == UDL_MEMORY_ACPI_RECLAIM)
    return 2;
  if (type == UDL_MEMORY_AVAILABLE)
    return 1;
  return 0;
}

udl_error
udl_normalize_memory_map(const udl_raw_memory_region *raw, udl_size raw_count,
                         udl_memory_map *output)
{
  udl_interval intervals[UDL_MAX_RAW_MEMORY_REGIONS + 1];
  udl_u64 points[(UDL_MAX_RAW_MEMORY_REGIONS + 1) * 2 + 2];
  udl_size interval_count = 0;
  udl_size point_count = 0;
  if (!raw || !output || raw_count > UDL_MAX_RAW_MEMORY_REGIONS)
    return UDL_BAD_ARGUMENT;
  output->count = 0;
  points[point_count++] = 0;
  points[point_count++] = UDL_LOW_RESERVED_LIMIT;
  for (udl_size i = 0; i < raw_count; ++i)
    {
      udl_u64 end;
      udl_u64 start;
      udl_u32 type;
      if (raw[i].length == 0)
        continue;
      end = raw[i].base + raw[i].length;
      if (end < raw[i].base)
        return UDL_BAD_MEMORY_MAP;
      start = raw[i].base;
      if (start >= UDL_MANAGED_LIMIT)
        continue;
      if (end > UDL_MANAGED_LIMIT)
        end = UDL_MANAGED_LIMIT;
      if (raw[i].type == UDL_GRUB_MEMORY_AVAILABLE)
        {
          start = round_up(start);
          end = round_down(end);
          type = UDL_MEMORY_AVAILABLE;
        }
      else
        {
          start = round_down(start);
          end = round_up(end);
          if (end > UDL_MANAGED_LIMIT)
            end = UDL_MANAGED_LIMIT;
          type = raw[i].type == UDL_GRUB_MEMORY_ACPI
            ? UDL_MEMORY_ACPI_RECLAIM : UDL_MEMORY_RESERVED;
        }
      if (start >= end)
        continue;
      if (interval_count >= UDL_MAX_RAW_MEMORY_REGIONS)
        return UDL_CAPACITY;
      intervals[interval_count].start = start;
      intervals[interval_count].end = end;
      intervals[interval_count].type = type;
      ++interval_count;
      points[point_count++] = start;
      points[point_count++] = end;
    }
  for (udl_size i = 1; i < point_count; ++i)
    {
      const udl_u64 value = points[i];
      udl_size j = i;
      while (j > 0 && points[j - 1] > value)
        {
          points[j] = points[j - 1];
          --j;
        }
      points[j] = value;
    }
  {
    udl_u64 previous = (udl_u64) -1;
    for (udl_size i = 0; i + 1 < point_count; ++i)
      {
        const udl_u64 start = points[i];
        const udl_u64 end = points[i + 1];
        udl_u32 selected = UDL_MEMORY_NONE;
        int selected_priority = 0;
        if (start == end || start == previous)
          continue;
        previous = start;
        if (start < UDL_LOW_RESERVED_LIMIT)
          selected = UDL_MEMORY_RESERVED;
        else
          for (udl_size r = 0; r < interval_count; ++r)
            if (intervals[r].start <= start && intervals[r].end >= end
                && interval_priority(intervals[r].type) > selected_priority)
              {
                selected = intervals[r].type;
                selected_priority = interval_priority(selected);
              }
        if (selected == UDL_MEMORY_NONE)
          continue;
        if (output->count > 0)
          {
            udl_memory_region *last = &output->regions[output->count - 1];
            if ((udl_u64) last->base + last->length == start
                && last->type == selected)
              {
                last->length += (udl_u32) (end - start);
                continue;
              }
          }
        if (output->count == UDL_MAX_MEMORY_REGIONS)
          return UDL_CAPACITY;
        output->regions[output->count].base = (udl_u32) start;
        output->regions[output->count].length = (udl_u32) (end - start);
        output->regions[output->count].type = (udl_u8) selected;
        output->regions[output->count].padding[0] = 0;
        output->regions[output->count].padding[1] = 0;
        output->regions[output->count].padding[2] = 0;
        ++output->count;
      }
  }
  return UDL_OK;
}

udl_error
udl_choose_physical_base(const udl_memory_map *map, udl_u32 span,
                         udl_u32 *base)
{
  if (!map || !base || span == 0 || span > UDL_MANAGED_LIMIT)
    return UDL_BAD_ARGUMENT;
  for (udl_size i = 0; i < map->count; ++i)
    {
      const udl_memory_region *region = &map->regions[i];
      const udl_u64 end = (udl_u64) region->base + region->length;
      if (region->type == UDL_MEMORY_AVAILABLE
          && region->base <= 0x01000000u
          && end >= 0x01000000ull + span)
        {
          *base = 0x01000000u;
          return UDL_OK;
        }
    }
  for (udl_size i = map->count; i > 0; --i)
    {
      const udl_memory_region *region = &map->regions[i - 1];
      const udl_u64 start = region->base < UDL_LOW_RESERVED_LIMIT
        ? UDL_LOW_RESERVED_LIMIT : region->base;
      const udl_u64 end = (udl_u64) region->base + region->length;
      udl_u64 candidate;
      if (region->type != UDL_MEMORY_AVAILABLE || end < start
          || end - start < span)
        continue;
      candidate = round_down(end - span);
      if (candidate < start || candidate + span > end
          || candidate + span > UDL_MANAGED_LIMIT)
        continue;
      *base = (udl_u32) candidate;
      return UDL_OK;
    }
  return UDL_NO_MEMORY;
}

const char *
udl_error_name(udl_error error)
{
  switch (error)
    {
    case UDL_OK: return "ok";
    case UDL_BAD_ARGUMENT: return "bad argument";
    case UDL_BAD_ELF: return "malformed ELF";
    case UDL_UNSUPPORTED_ELF: return "unsupported ELF";
    case UDL_CAPACITY: return "capacity exceeded";
    case UDL_BAD_MEMORY_MAP: return "invalid memory map";
    case UDL_NO_MEMORY: return "no usable physical span";
    case UDL_UNRESOLVED_SYMBOL: return "unresolved symbol";
    case UDL_BAD_RELOCATION: return "invalid relocation";
    case UDL_BAD_LAYOUT: return "invalid image layout";
    }
  return "unknown loader error";
}
