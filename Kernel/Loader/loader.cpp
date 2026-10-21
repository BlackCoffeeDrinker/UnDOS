#include "../../ABI/userspace.h"
#include <undos/syscalls.h>
#include <undos/abi.h>
#include <undos/abi_note.hpp>

// Preserve the kernel's argc/argv block above the loader's working stack.
extern "C" void _start();
extern "C" char __loader_image_start[];
extern "C" char __loader_image_end[];
asm(
    ".text\n\t"
    ".global _start\n\t"
    ".type _start, @function\n\t"
    "_start:\n\t"
    "    movl 4(%esp), %eax\n\t"
    "    movl 12(%eax), %eax\n\t"
    "    movl (%eax), %eax\n\t"
    "    movl %esp, %edx\n\t"
    "    movl 8(%esp), %ecx\n\t"
    "    subl $4, %esp\n\t"
    "    pushl %ecx\n\t"
    "    pushl %edx\n\t"
    "    pushl %eax\n\t"
    "    call loader_main\n\t"
    ".size _start, . - _start\n\t");

namespace {

// ============================================================================
// ELF32 Specification Constants & Structures
// ============================================================================

// ELF Identification Indices & Values
constexpr uint8_t ELF_MAG0 = 0x7f;
constexpr uint8_t ELF_MAG1 = 'E';
constexpr uint8_t ELF_MAG2 = 'L';
constexpr uint8_t ELF_MAG3 = 'F';
constexpr uint8_t ELF_CLASS32 = 1;
constexpr uint8_t ELF_DATA2LSB = 1;
constexpr uint8_t ELF_EV_CURRENT = 1;

// ELF File Types & Architecture
constexpr uint16_t ET_EXEC = 2;
constexpr uint16_t ET_DYN = 3;
constexpr uint16_t EM_386 = 3;
constexpr uint32_t EV_CURRENT = 1;

// Program Header Types & Permission Flags
constexpr uint32_t PT_LOAD = 1;
constexpr uint32_t PT_DYNAMIC = 2;
constexpr uint32_t PT_INTERP = 3;
constexpr uint32_t PT_NOTE = 4;
constexpr uint32_t PT_TLS = 7;
constexpr uint32_t PF_X = 1;
constexpr uint32_t PF_W = 2;
constexpr uint32_t PF_R = 4;

// Dynamic Array Tags (d_tag)
constexpr int32_t DT_NULL = 0;
constexpr int32_t DT_NEEDED = 1;
constexpr int32_t DT_PLTRELSZ = 2;
constexpr int32_t DT_HASH = 4;
constexpr int32_t DT_STRTAB = 5;
constexpr int32_t DT_SYMTAB = 6;
constexpr int32_t DT_STRSZ = 10;
constexpr int32_t DT_SYMENT = 11;
constexpr int32_t DT_INIT = 12;
constexpr int32_t DT_FINI = 13;
constexpr int32_t DT_REL = 17;
constexpr int32_t DT_RELSZ = 18;
constexpr int32_t DT_RELENT = 19;
constexpr int32_t DT_PLTREL = 20;
constexpr int32_t DT_JMPREL = 23;
constexpr int32_t DT_INIT_ARRAY = 25;
constexpr int32_t DT_FINI_ARRAY = 26;
constexpr int32_t DT_INIT_ARRAYSZ = 27;
constexpr int32_t DT_FINI_ARRAYSZ = 28;

// Symbol Binding, Visibility, and Special Section Indices
constexpr uint8_t STB_LOCAL = 0;
constexpr uint8_t STB_GLOBAL = 1;
constexpr uint8_t STB_WEAK = 2;
constexpr uint8_t STV_HIDDEN = 2;
constexpr uint16_t SHN_UNDEF = 0;

// i386 Relocation Types
constexpr uint32_t R_386_NONE = 0;
constexpr uint32_t R_386_32 = 1;
constexpr uint32_t R_386_PC32 = 2;
constexpr uint32_t R_386_COPY = 5;
constexpr uint32_t R_386_GLOB_DAT = 6;
constexpr uint32_t R_386_JMP_SLOT = 7;
constexpr uint32_t R_386_RELATIVE = 8;

// ============================================================================
// Memory Management & Resource Limits (Sized for 4MB RAM target)
// ============================================================================

constexpr uint32_t PAGE_SIZE = 4096;
constexpr uint32_t PAGE_MASK = ~(PAGE_SIZE - 1);
constexpr uint32_t MAX_OBJECTS = 8;
constexpr uint32_t MAX_IMAGE_SIZE = 4 * 1024 * 1024; // 4 MiB max image size
constexpr uint32_t MAX_PROGRAM_HEADERS = 16;
constexpr uint32_t MAX_DEPENDENCIES = 32;
constexpr uint32_t MAX_PATH_LENGTH = 256;

// Address Space Layout Invariants
constexpr uint32_t USER_SPACE_MIN_ADDR = 0x00010000;    // Lower 64KB reserved (null page trap)
constexpr uint32_t USER_SPACE_MAX_ADDR = 0xC0000000;    // 3GB userland limit (kernel boundary)
constexpr uint32_t SHARED_LIB_BASE_WINDOW = 0x10000000;// Base virtual address for shared libraries
constexpr uint32_t SHARED_LIB_SLOT_SIZE = 0x01000000;  // 16 MiB virtual slot per loaded shared library

// ============================================================================
// Data Structures
// ============================================================================

struct ElfHeader {
  uint8_t ident[16];
  uint16_t type;
  uint16_t machine;
  uint32_t version;
  uint32_t entry;
  uint32_t phoff;
  uint32_t shoff;
  uint32_t flags;
  uint16_t ehsize;
  uint16_t phentsize;
  uint16_t phnum;
  uint16_t shentsize;
  uint16_t shnum;
  uint16_t shstrndx;
};

struct ProgramHeader {
  uint32_t type;
  uint32_t offset;
  uint32_t vaddr;
  uint32_t paddr;
  uint32_t filesz;
  uint32_t memsz;
  uint32_t flags;
  uint32_t align;
};

struct Dynamic {
  int32_t tag;
  uint32_t value;
};

struct Symbol {
  uint32_t name;
  uint32_t value;
  uint32_t size;
  uint8_t info;
  uint8_t other;
  uint16_t section;
};

struct Rel {
  uint32_t offset;
  uint32_t info;
};

struct LoadedObject {
  char path[MAX_PATH_LENGTH];
  ProgramHeader program_headers[MAX_PROGRAM_HEADERS];
  uint32_t phnum{0};
  uint32_t bias{0};               // Load bias (virtual address offset for ET_DYN)
  uint32_t entry{0};              // Entry point address
  uint32_t type{0};               // ET_EXEC or ET_DYN
  uint32_t low_vaddr{0};          // Lowest page-aligned virtual address
  uint32_t high_vaddr{0};         // Highest virtual address bound

  // Dynamic linking metadata
  uint32_t strtab_vaddr{0};       // Virtual address of string table
  uint32_t strtab_size{0};        // Size in bytes of string table
  uint32_t symtab_vaddr{0};       // Virtual address of symbol table
  uint32_t syment_size{0};        // Size of each symbol entry
  uint32_t symbol_count{0};       // Total symbols in table (from DT_HASH)

  uint32_t rel_vaddr{0};          // Virtual address of REL table
  uint32_t rel_size{0};           // Total size of REL table
  uint32_t rel_ent_size{0};       // Size of each REL entry
  uint32_t plt_rel_vaddr{0};      // Virtual address of PLT REL table (DT_JMPREL)
  uint32_t plt_rel_size{0};       // Total size of PLT REL table (DT_PLTRELSZ)
  uint32_t plt_rel_type{0};       // Type of PLT relocations (DT_PLTREL, expected DT_REL = 17)

  uint32_t init_func_vaddr{0};    // Single initialization function (DT_INIT)
  uint32_t init_array_vaddr{0};   // Array of initialization functions (DT_INIT_ARRAY)
  uint32_t init_array_size{0};    // Size in bytes of DT_INIT_ARRAY
  uint32_t fini_func_vaddr{0};
  uint32_t fini_array_vaddr{0};
  uint32_t fini_array_size{0};

  uint32_t needed_offsets[MAX_DEPENDENCIES]; // Offsets into strtab for DT_NEEDED
  uint32_t dependencies[MAX_DEPENDENCIES]{};
  uint32_t needed_count{0};
};

LoadedObject objects[MAX_OBJECTS]{};
uint32_t object_count = 0;
uint32_t total_mapped_bytes = 0;
const char *load_failure_reason = "ELoader: invalid ELF header\n";
uint32_t initialization_order[MAX_OBJECTS]{};
uint32_t initialization_count{};
uint32_t initialized_count{};
uint8_t visit_state[MAX_OBJECTS]{};

// ============================================================================
// Virtual Address & Memory Range Validation Helpers
// ============================================================================

// Dynamic addresses in ELF headers are virtual addresses, not host pointers.
// Verifies that [vaddr, vaddr + size) is completely contained within one of the object's PT_LOAD segments.
bool is_valid_virtual_range(const LoadedObject &obj, uint32_t vaddr, uint32_t size) {
  if (vaddr > 0xffffffffu - size) {
    return false;
  }
  for (uint32_t i = 0; i < obj.phnum; ++i) {
    const auto &p = obj.program_headers[i];
    if (p.type == PT_LOAD && vaddr >= p.vaddr && size <= p.memsz && (vaddr - p.vaddr) <= (p.memsz - size)) {
      return true;
    }
  }
  return false;
}

// Translates an ELF virtual address to a host pointer address by applying the object's load bias.
// Ensures that the range is valid and no integer overflow occurs.
bool resolve_virtual_to_host_address(const LoadedObject &obj, uint32_t vaddr, uint32_t size, uint32_t &host_addr) {
  if (!is_valid_virtual_range(obj, vaddr, size) || vaddr > 0xffffffffu - obj.bias) {
    return false;
  }
  host_addr = vaddr + obj.bias;
  return true;
}

// Translates a table base address and element count to host address, with overflow checks.
bool resolve_table_address(const LoadedObject &obj, uint32_t vaddr, uint32_t count, uint32_t elem_size, uint32_t &host_addr) {
  if (elem_size == 0 || count > MAX_IMAGE_SIZE / elem_size) {
    return false;
  }
  return resolve_virtual_to_host_address(obj, vaddr, count * elem_size, host_addr);
}

// Extracts a null-terminated string from the object's string table at the specified offset.
bool get_string_at(const LoadedObject &obj, uint32_t offset, const char *&result_str) {
  uint32_t host_addr = 0;
  if (!obj.strtab_vaddr || offset >= obj.strtab_size || obj.strtab_vaddr > 0xffffffffu - offset ||
      !resolve_virtual_to_host_address(obj, obj.strtab_vaddr + offset, obj.strtab_size - offset, host_addr)) {
    return false;
  }
  result_str = reinterpret_cast<const char *>(host_addr);
  for (uint32_t i = 0; i < obj.strtab_size - offset; ++i) {
    if (result_str[i] == '\0') {
      return true;
    }
  }
  return false;
}

// Compares two null-terminated strings for equality.
bool string_equals(const char *a, const char *b) {
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}

// Reads a symbol entry by index from the object's symbol table.
bool get_symbol_at(const LoadedObject &obj, uint32_t index, Symbol &symbol) {
  uint32_t host_addr = 0;
  if (index >= obj.symbol_count || obj.symtab_vaddr > 0xffffffffu - index * sizeof(Symbol) ||
      !resolve_table_address(obj, obj.symtab_vaddr + index * sizeof(Symbol), 1, sizeof(Symbol), host_addr)) {
    return false;
  }
  symbol = *reinterpret_cast<const Symbol *>(host_addr);
  return true;
}

// ============================================================================
// File I/O Helpers
// ============================================================================

// Reads exact byte count from handle into destination, breaking requests into small chunks
// to handle partial reads from the kernel I/O subsystem.
bool read_exact_bytes(HANDLE handle, void *destination, uint32_t size) {
  auto *bytes = static_cast<uint8_t *>(destination);
  while (size > 0) {
    const uint32_t chunk_request = (size < 1024) ? size : 1024;
    uint32_t bytes_read = chunk_request;
    if (UD_SYS_ReadV1(handle, bytes, &bytes_read) != SyscallReturnCode_OKAY ||
        bytes_read == 0 || bytes_read > chunk_request) {
      return false;
    }
    bytes += bytes_read;
    size -= bytes_read;
  }
  return true;
}

// Advances file offset by reading and discarding bytes.
bool skip_bytes(HANDLE handle, uint32_t size) {
  uint8_t scratch[128];
  while (size > 0) {
    const uint32_t chunk = (size < sizeof(scratch)) ? size : sizeof(scratch);
    if (!read_exact_bytes(handle, scratch, chunk)) {
      return false;
    }
    size -= chunk;
  }
  return true;
}

// ============================================================================
// Object Loading Pipeline Stages
// ============================================================================

// Reads and validates the ELF header and program header table from an open file.
bool read_and_validate_headers(LoadedObject &obj, HANDLE handle, ElfHeader &header) {
  load_failure_reason = "ELoader: invalid ELF header\n";
  if (!read_exact_bytes(handle, &header, sizeof(header))) {
    return false;
  }

  // Validate ELF magic and 32-bit little-endian current-version properties
  const bool magic_valid = (header.ident[0] == ELF_MAG0 && header.ident[1] == ELF_MAG1 &&
                            header.ident[2] == ELF_MAG2 && header.ident[3] == ELF_MAG3 &&
                            header.ident[4] == ELF_CLASS32 && header.ident[5] == ELF_DATA2LSB &&
                            header.ident[6] == ELF_EV_CURRENT);
  const bool format_valid = ((header.type == ET_EXEC || header.type == ET_DYN) &&
                             header.machine == EM_386 && header.version == EV_CURRENT &&
                             header.ehsize == sizeof(ElfHeader) &&
                             header.phentsize == sizeof(ProgramHeader));
  const bool phdr_bounds_valid = (header.phnum > 0 && header.phnum <= MAX_PROGRAM_HEADERS &&
                                  header.phoff >= sizeof(ElfHeader) &&
                                  header.phoff <= MAX_IMAGE_SIZE - header.phnum * sizeof(ProgramHeader));

  if (!magic_valid || !format_valid || !phdr_bounds_valid) {
    return false;
  }

  // Read program header table
  if (!skip_bytes(handle, header.phoff - sizeof(ElfHeader)) ||
      !read_exact_bytes(handle, obj.program_headers, header.phnum * sizeof(ProgramHeader))) {
    return false;
  }

  obj.phnum = header.phnum;
  obj.type = header.type;
  obj.entry = header.entry;
  return true;
}

// Calculates the virtual address span and assigns the load bias for shared objects (ET_DYN).
bool calculate_bounds_and_bias(LoadedObject &obj, const ElfHeader &header) {
  load_failure_reason = "ELoader: invalid ELF segments\n";
  obj.low_vaddr = 0xffffffffu;
  obj.high_vaddr = 0;

  for (uint32_t i = 0; i < obj.phnum; ++i) {
    const auto &p = obj.program_headers[i];
    if (p.type != PT_LOAD || p.memsz == 0) {
      continue;
    }

    // Segment size, offset, and virtual address arithmetic overflow checks
    if (p.filesz > p.memsz || p.vaddr > 0xffffffffu - p.memsz ||
        p.offset > MAX_IMAGE_SIZE || p.filesz > MAX_IMAGE_SIZE - p.offset ||
        p.vaddr + p.memsz > 0xffffffffu - (PAGE_SIZE - 1)) {
      return false;
    }

    const uint32_t segment_low = p.vaddr & PAGE_MASK;
    const uint32_t segment_high = p.vaddr + p.memsz;
    if (segment_low < obj.low_vaddr) {
      obj.low_vaddr = segment_low;
    }
    if (segment_high > obj.high_vaddr) {
      obj.high_vaddr = segment_high;
    }
  }

  if (obj.low_vaddr == 0xffffffffu || (obj.high_vaddr - obj.low_vaddr) > MAX_IMAGE_SIZE) {
    return false;
  }

  if (obj.type == ET_DYN) {
    // Shared libraries (ET_DYN) are assigned non-overlapping 16 MiB virtual slots starting at 0x10000000.
    // The load bias is the difference between the assigned slot base and the image's low address.
    const uint32_t slot = SHARED_LIB_BASE_WINDOW + (object_count - 1) * SHARED_LIB_SLOT_SIZE;
    if (obj.low_vaddr > slot || obj.high_vaddr > slot + SHARED_LIB_SLOT_SIZE) {
      return false;
    }
    obj.bias = slot - obj.low_vaddr;
  }

  // Validate main executable entry point falls inside an executable PT_LOAD segment
  if (object_count == 1) {
    uint32_t host_entry = 0;
    if (!resolve_virtual_to_host_address(obj, obj.entry, 1, host_entry)) {
      return false;
    }
    bool entry_is_executable = false;
    for (uint32_t i = 0; i < obj.phnum; ++i) {
      const auto &p = obj.program_headers[i];
      if (p.type == PT_LOAD && (p.flags & PF_X) && header.entry >= p.vaddr && (header.entry - p.vaddr) < p.memsz) {
        entry_is_executable = true;
        break;
      }
    }
    if (!entry_is_executable) {
      return false;
    }
  }

  return true;
}

// Maps userland pages with fixed virtual addresses and loads segment contents from disk.
bool map_and_load_segments(LoadedObject &obj) {
  load_failure_reason = "ELoader: cannot map image pages\n";
  const auto loader_begin = reinterpret_cast<uintptr_t>(__loader_image_start);
  const auto loader_end = reinterpret_cast<uintptr_t>(__loader_image_end);

  for (uint32_t i = 0; i < obj.phnum; ++i) {
    const auto &p = obj.program_headers[i];
    if (p.type != PT_LOAD || p.memsz == 0) {
      continue;
    }

    const uint32_t begin = (p.vaddr + obj.bias) & PAGE_MASK;
    const uint32_t end = (p.vaddr + p.memsz + obj.bias + (PAGE_SIZE - 1)) & PAGE_MASK;

    // Protect the loader's complete linked image, including page-rounded BSS.
    load_failure_reason = "ELoader: image overlaps reserved memory\n";
    if (begin < USER_SPACE_MIN_ADDR ||
        (begin < loader_end && end > loader_begin) ||
        end > USER_SPACE_MAX_ADDR) {
      return false;
    }

    // Map each page in the segment, avoiding duplicate mappings for pages shared across adjacent PT_LOAD segments
    for (uint32_t page = begin; page < end; page += PAGE_SIZE) {
      bool already_mapped = false;
      for (uint32_t j = 0; j < i; ++j) {
        const auto &prev = obj.program_headers[j];
        if (prev.type == PT_LOAD && prev.memsz > 0) {
          const uint32_t prev_begin = (prev.vaddr + obj.bias) & PAGE_MASK;
          const uint32_t prev_end = (prev.vaddr + prev.memsz + obj.bias + (PAGE_SIZE - 1)) & PAGE_MASK;
          if (page >= prev_begin && page < prev_end) {
            already_mapped = true;
            break;
          }
        }
      }

      if (!already_mapped) {
        load_failure_reason = "ELoader: fixed page allocation failed\n";
        if (total_mapped_bytes > MAX_IMAGE_SIZE - PAGE_SIZE) {
          return false;
        }
        // Map page as Read/Write/Execute in user address space
        const auto prot = static_cast<MemoryProtectFlags>(
            MemoryProtectFlags_Read | MemoryProtectFlags_Write | MemoryProtectFlags_Execute);
        if (UD_SYS_MapFixedPagesV1(page, PAGE_SIZE, prot) != SyscallReturnCode_OKAY) {
          return false;
        }
        total_mapped_bytes += PAGE_SIZE;
      }
    }

    // Read segment file data from disk into mapped memory
    load_failure_reason = "ELoader: failed to read image segment\n";
    if (p.filesz > 0) {
      HANDLE handle = 0;
      if (UD_SYS_OpenV1(obj.path, FileOpenMode_Read, &handle) != SyscallReturnCode_OKAY) {
        return false;
      }
      const bool read_ok = skip_bytes(handle, p.offset) &&
                           read_exact_bytes(handle, reinterpret_cast<void *>(p.vaddr + obj.bias), p.filesz);
      (void) UD_SYS_CloseV1(handle);
      if (!read_ok) {
        return false;
      }
    }

    // Zero out BSS section (bytes in memory beyond filesz up to memsz)
    auto *bss_tail = reinterpret_cast<uint8_t *>(p.vaddr + obj.bias + p.filesz);
    for (uint32_t n = p.filesz; n < p.memsz; ++n) {
      bss_tail[n - p.filesz] = 0;
    }
  }

  return true;
}

// Parses PT_DYNAMIC segment and records dynamic tags (DT_NEEDED, DT_HASH, DT_STRTAB, DT_SYMTAB, DT_REL, etc.).
bool parse_dynamic_segment(LoadedObject &obj) {
  for (uint32_t i = 0; i < obj.phnum; ++i) {
    const auto &p = obj.program_headers[i];
    if (p.type != PT_DYNAMIC) {
      continue;
    }

    load_failure_reason = "ELoader: invalid dynamic segment\n";
    uint32_t host_addr = 0;
    if (p.memsz == 0 || p.memsz > PAGE_SIZE || (p.memsz % sizeof(Dynamic)) != 0 ||
        !resolve_virtual_to_host_address(obj, p.vaddr, p.memsz, host_addr)) {
      return false;
    }

    bool null_terminated = false;
    const uint32_t entry_count = p.memsz / sizeof(Dynamic);
    const auto *dyn_entries = reinterpret_cast<const Dynamic *>(host_addr);

    for (uint32_t n = 0; n < entry_count; ++n) {
      const auto &d = dyn_entries[n];
      if (d.tag == DT_NULL) {
        null_terminated = true;
        break;
      }

      switch (d.tag) {
        case DT_NEEDED:
          if (obj.needed_count >= MAX_DEPENDENCIES) {
            return false;
          }
          obj.needed_offsets[obj.needed_count++] = d.value;
          break;
        case DT_HASH: {
          uint32_t hash_host_addr = 0;
          if (!resolve_virtual_to_host_address(obj, d.value, 8, hash_host_addr)) {
            return false;
          }
          // The second 32-bit word in DT_HASH header is `nchain` (total symbol count)
          obj.symbol_count = reinterpret_cast<const uint32_t *>(hash_host_addr)[1];
          break;
        }
        case DT_STRTAB:
          obj.strtab_vaddr = d.value;
          break;
        case DT_SYMTAB:
          obj.symtab_vaddr = d.value;
          break;
        case DT_STRSZ:
          obj.strtab_size = d.value;
          break;
        case DT_SYMENT:
          obj.syment_size = d.value;
          break;
        case DT_REL:
          obj.rel_vaddr = d.value;
          break;
        case DT_RELSZ:
          obj.rel_size = d.value;
          break;
        case DT_RELENT:
          obj.rel_ent_size = d.value;
          break;
        case DT_PLTREL:
          obj.plt_rel_type = d.value;
          break;
        case DT_JMPREL:
          obj.plt_rel_vaddr = d.value;
          break;
        case DT_PLTRELSZ:
          obj.plt_rel_size = d.value;
          break;
        case DT_INIT:
          obj.init_func_vaddr = d.value;
          break;
        case DT_FINI:
          obj.fini_func_vaddr = d.value;
          break;
        case DT_FINI_ARRAY:
          obj.fini_array_vaddr = d.value;
          break;
        case DT_FINI_ARRAYSZ:
          obj.fini_array_size = d.value;
          break;
        case 7: // DT_RELA: i386 ABI v1 supports REL only.
        case 8: // DT_RELASZ
        case 35: // DT_RELRSZ
        case 36: // DT_RELR
        case 37: // DT_RELRENT
        case 0x6ffffff0: // DT_VERSYM
        case 0x6ffffffc: // DT_VERDEF
        case 0x6ffffffe: // DT_VERNEED
          if (d.value) return false;
          break;
        case DT_INIT_ARRAY:
          obj.init_array_vaddr = d.value;
          break;
        case DT_INIT_ARRAYSZ:
          obj.init_array_size = d.value;
          break;
        default:
          break;
      }
    }

    if (!null_terminated) {
      return false;
    }
  }

  return true;
}

// Validates symbol table and string table boundaries against loaded image size.
bool validate_symbol_tables(LoadedObject &obj) {
  if (obj.symbol_count > 0 || obj.needed_count > 0 || obj.rel_size > 0 || obj.plt_rel_size > 0) {
    load_failure_reason = "ELoader: invalid symbol tables\n";
    uint32_t host_addr = 0;
    if (obj.syment_size != sizeof(Symbol) || obj.symbol_count == 0 ||
        obj.symbol_count > MAX_IMAGE_SIZE / sizeof(Symbol) ||
        !resolve_table_address(obj, obj.symtab_vaddr, obj.symbol_count, sizeof(Symbol), host_addr) ||
        obj.strtab_size == 0 || obj.strtab_size > MAX_IMAGE_SIZE ||
        !resolve_virtual_to_host_address(obj, obj.strtab_vaddr, obj.strtab_size, host_addr)) {
      return false;
    }
  }
  for (uint32_t i = 0; i < obj.symbol_count; ++i) {
    Symbol symbol{};
    if (!get_symbol_at(obj, i, symbol)) return false;
    const auto type = symbol.info & 15;
    if (type == 6 || type == 10) {
      load_failure_reason = "ELoader: TLS and IFUNC symbols are unsupported\n";
      return false;
    }
  }
  return true;
}

// Coordinates the full loading pipeline for a single ELF object (executable or shared library).
bool load_object(LoadedObject &obj) {
  load_failure_reason = "ELoader: cannot open image\n";
  HANDLE handle = 0;
  if (UD_SYS_OpenV1(obj.path, FileOpenMode_Read, &handle) != SyscallReturnCode_OKAY) {
    return false;
  }

  ElfHeader header{};
  const bool headers_ok = read_and_validate_headers(obj, handle, header);
  (void) UD_SYS_CloseV1(handle);
  if (!headers_ok) {
    return false;
  }

  if (!calculate_bounds_and_bias(obj, header)) {
    return false;
  }

  if (!map_and_load_segments(obj)) {
    return false;
  }

  load_failure_reason = "ELoader: missing or incompatible UnDOS ABI note\n";
  uint32_t notes = 0;
  for (uint32_t i = 0; i < obj.phnum; ++i) {
    const auto &p = obj.program_headers[i];
    if (p.type == PT_INTERP) {
      load_failure_reason = "ELoader: external ELF interpreters are unsupported\n";
      return false;
    }
    if (p.type == PT_TLS) {
      load_failure_reason = "ELoader: TLS is unsupported by ABI v1\n";
      return false;
    }
    if (p.type != PT_NOTE) continue;
    uint32_t address = 0;
    if (!resolve_virtual_to_host_address(obj, p.vaddr, p.filesz, address) ||
        !undos::abi::scan_notes(reinterpret_cast<const uint8_t *>(address), p.filesz, notes)) return false;
  }
  if (notes != 1) return false;

  if (!parse_dynamic_segment(obj)) {
    return false;
  }

  if (!validate_symbol_tables(obj)) {
    return false;
  }

  return true;
}

// ============================================================================
// Symbol Resolution & Relocation Engine
// ============================================================================

// Searches for a symbol definition by name across all loaded objects in dependency order.
// Supports strong vs. weak binding precedence and enforces that COPY relocations source
// definitions from shared libraries rather than the main executable.
bool resolve_symbol(const char *name, uint32_t &resolved_value, Symbol &found_symbol,
                    uint32_t &owner_index, bool is_copy_reloc) {
  bool weak_found = false;

  for (uint32_t i = 0; i < object_count; ++i) {
    // For COPY relocations, skip the main executable (index 0) to find the original definition in a library
    if (is_copy_reloc && i == 0) {
      continue;
    }

    const LoadedObject &obj = objects[i];
    for (uint32_t j = 1; j < obj.symbol_count; ++j) {
      Symbol sym{};
      const char *candidate_name = nullptr;
      if (!get_symbol_at(obj, j, sym) || !get_string_at(obj, sym.name, candidate_name)) {
        return false;
      }

      // Check for undefined symbol, local binding, or hidden visibility
      const uint8_t binding = sym.info >> 4;
      const uint8_t visibility = sym.other & 3;
      if (sym.section == SHN_UNDEF || binding == STB_LOCAL || visibility == STV_HIDDEN ||
          !string_equals(name, candidate_name)) {
        continue;
      }

      // Validate symbol value lies within a valid mapped segment
      const uint32_t sym_size = (sym.size > 0) ? sym.size : 1;
      if (sym.value > 0xffffffffu - obj.bias || !is_valid_virtual_range(obj, sym.value, sym_size)) {
        return false;
      }

      // Weak definitions are remembered but can be overridden by later strong definitions
      if (binding == STB_WEAK && !weak_found) {
        weak_found = true;
        resolved_value = sym.value + obj.bias;
        found_symbol = sym;
        owner_index = i;
        continue;
      }

      // Strong definition wins immediately
      if (binding != STB_WEAK) {
        resolved_value = sym.value + obj.bias;
        found_symbol = sym;
        owner_index = i;
        return true;
      }
    }
  }

  return weak_found;
}

// Applies a single relocation entry (i386 REL format) to the target memory location.
bool apply_single_relocation(LoadedObject &obj, const Rel &reloc, bool copy_only) {
  uint32_t target_host_addr = 0;
  if (!resolve_virtual_to_host_address(obj, reloc.offset, sizeof(uint32_t), target_host_addr)) {
    return false;
  }

  const uint32_t reloc_type = reloc.info & 0xff;
  const uint32_t sym_index = reloc.info >> 8;

  if (reloc_type == R_386_NONE) {
    return true;
  }

  // Filter based on whether this pass is specifically for COPY relocations or standard relocations
  if ((reloc_type == R_386_COPY) != copy_only) {
    return true;
  }

  uint32_t sym_value = 0;
  uint32_t owner_index = 0;
  Symbol sym{}, def_symbol{};

  if (reloc_type != R_386_RELATIVE) {
    const char *sym_name = nullptr;
    if (!get_symbol_at(obj, sym_index, sym) || !get_string_at(obj, sym.name, sym_name)) {
      return false;
    }

    const uint8_t binding = sym.info >> 4;
    // If the symbol is local and defined in a valid section, use its biased value directly
    if (binding == STB_LOCAL && sym.section != SHN_UNDEF) {
      const uint32_t sym_size = (sym.size > 0) ? sym.size : 1;
      if (!is_valid_virtual_range(obj, sym.value, sym_size) || sym.value > 0xffffffffu - obj.bias) {
        return false;
      }
      sym_value = sym.value + obj.bias;
    } else if (!resolve_symbol(sym_name, sym_value, def_symbol, owner_index, reloc_type == R_386_COPY)) {
      // Unresolved weak symbols resolve to address 0; unresolved strong symbols are fatal errors
      if (binding != STB_WEAK) {
        return false;
      }
      sym_value = 0;
    }
  }

  auto &target_word = *reinterpret_cast<uint32_t *>(target_host_addr);

  switch (reloc_type) {
    case R_386_RELATIVE:
      // B + A: Add load bias to addend in place
      target_word += obj.bias;
      break;
    case R_386_32:
      // S + A: Add symbol value to addend in place
      target_word += sym_value;
      break;
    case R_386_PC32:
      // S + A - P: Symbol value plus addend minus relocation target address
      target_word += sym_value - target_host_addr;
      break;
    case R_386_GLOB_DAT:
    case R_386_JMP_SLOT:
      // S: Set target word to symbol value
      target_word = sym_value;
      break;
    case R_386_COPY: {
      // Copy data from shared library symbol definition to executable BSS location
      if (sym_value == 0 || sym.size > def_symbol.size || sym.size > MAX_IMAGE_SIZE ||
          !is_valid_virtual_range(obj, reloc.offset, sym.size) ||
          !is_valid_virtual_range(objects[owner_index], def_symbol.value, sym.size)) {
        return false;
      }
      auto *dest = reinterpret_cast<uint8_t *>(target_host_addr);
      const auto *src = reinterpret_cast<const uint8_t *>(sym_value);
      for (uint32_t n = 0; n < sym.size; ++n) {
        dest[n] = src[n];
      }
      break;
    }
    default:
      return false;
  }

  return true;
}

// Processes all relocations in a REL table for the given object.
bool relocate_table(LoadedObject &obj, uint32_t virtual_address, uint32_t bytes, bool copy_only) {
  if (bytes == 0) {
    return true;
  }

  uint32_t host_addr = 0;
  if ((obj.rel_ent_size != 0 && obj.rel_ent_size != sizeof(Rel)) ||
      (bytes % sizeof(Rel)) != 0 ||
      !resolve_table_address(obj, virtual_address, bytes / sizeof(Rel), sizeof(Rel), host_addr)) {
    return false;
  }

  const uint32_t count = bytes / sizeof(Rel);
  const auto *relocs = reinterpret_cast<const Rel *>(host_addr);

  for (uint32_t i = 0; i < count; ++i) {
    if (!apply_single_relocation(obj, relocs[i], copy_only)) {
      return false;
    }
  }

  return true;
}

// ============================================================================
// Dependency Discovery & Resolution
// ============================================================================

// Formulates and tests library file paths (executable directory first, then /System/),
// registers the library if not already loaded, and initiates its loading.
bool find_and_load_dependency(const char *name, const char *executable_path, uint32_t &index) {
  // Validate dependency name length and prohibit path traversal characters
  uint32_t name_len = 0;
  while (name[name_len] != '\0' && name_len < 127) {
    ++name_len;
  }
  if (name_len == 0 || name_len >= 127) {
    return false;
  }
  for (uint32_t i = 0; i < name_len; ++i) {
    if (name[i] == '/' || (name[i] == '.' && (i + 1 < name_len) && name[i + 1] == '.')) {
      return false;
    }
  }

  char path[MAX_PATH_LENGTH]{};
  uint32_t dir_len = 0;

  // Search strategy 1: Executable's directory
  for (uint32_t i = 0; executable_path[i] != '\0' && i < 240; ++i) {
    if (executable_path[i] == '/') {
      dir_len = i + 1;
    }
  }

  if (dir_len + name_len < sizeof(path)) {
    for (uint32_t i = 0; i < dir_len; ++i) {
      path[i] = executable_path[i];
    }
    for (uint32_t i = 0; i <= name_len; ++i) {
      path[dir_len + i] = name[i];
    }
    HANDLE test_handle = 0;
    if (UD_SYS_OpenV1(path, FileOpenMode_Read, &test_handle) == SyscallReturnCode_OKAY) {
      (void) UD_SYS_CloseV1(test_handle);
    } else {
      dir_len = 0;
    }
  } else {
    dir_len = 0;
  }

  // Search strategy 2: /System/ system directory
  if (dir_len == 0) {
    const char system_prefix[] = "/System/";
    for (uint32_t i = 0; i < sizeof(system_prefix) - 1; ++i) {
      path[i] = system_prefix[i];
    }
    for (uint32_t i = 0; i <= name_len; ++i) {
      path[sizeof(system_prefix) - 1 + i] = name[i];
    }
  }

  // Deduplicate against already-loaded objects to prevent cycles and duplicate loads
  for (uint32_t i = 0; i < object_count; ++i) {
    if (string_equals(objects[i].path, path)) {
      index = i;
      return true;
    }
  }

  if (object_count >= MAX_OBJECTS) {
    return false;
  }

  index = object_count++;
  LoadedObject &new_obj = objects[index];
  for (uint32_t i = 0; i < sizeof(path); ++i) {
    new_obj.path[i] = path[i];
  }

  return load_object(new_obj) && (new_obj.type == ET_DYN);
}

// ============================================================================
// Initialization & Execution
// ============================================================================

// Executes DT_INIT functions and DT_INIT_ARRAY function pointers in reverse discovery order
// so that lower-level dependencies are initialized before dependents.
bool executable_address(uint32_t address) {
  for (uint32_t i = 0; i < object_count; ++i) {
    const auto &obj = objects[i];
    if (address < obj.bias) continue;
    const auto vaddr = address - obj.bias;
    for (uint32_t j = 0; j < obj.phnum; ++j) {
      const auto &p = obj.program_headers[j];
      if (p.type == PT_LOAD && (p.flags & PF_X) && vaddr >= p.vaddr && vaddr - p.vaddr < p.memsz)
        return true;
    }
  }
  return false;
}

bool visit_dependencies(uint32_t index) {
  if (visit_state[index] == 2) return true;
  if (visit_state[index] == 1) return false;
  visit_state[index] = 1;
  const auto &obj = objects[index];
  for (uint32_t i = 0; i < obj.needed_count; ++i) {
    if (!visit_dependencies(obj.dependencies[i])) return false;
  }
  visit_state[index] = 2;
  if (index) initialization_order[initialization_count++] = index;
  return true;
}

bool lifecycle_function(const LoadedObject &obj, uint32_t address, bool run) {
  if (!address) return true;
  uint32_t host = 0;
  if (!resolve_virtual_to_host_address(obj, address, 1, host) || !executable_address(host)) return false;
  if (run) reinterpret_cast<void (*)()>(host)();
  return true;
}

bool lifecycle_array(const LoadedObject &obj, uint32_t address, uint32_t size, bool reverse, bool run) {
  if (!size) return true;
  uint32_t host = 0;
  if (size % 4 || !resolve_virtual_to_host_address(obj, address, size, host)) return false;
  const auto *functions = reinterpret_cast<const uint32_t *>(host);
  for (uint32_t i = 0; i < size / 4; ++i) {
    const auto function = functions[reverse ? size / 4 - i - 1 : i];
    if (!function || function == UINT32_MAX) continue;
    if (!executable_address(function)) return false;
    if (run) reinterpret_cast<void (*)()>(function)();
  }
  return true;
}

bool validate_lifecycle() {
  for (uint32_t i = 1; i < object_count; ++i) {
    const auto &obj = objects[i];
    if (!lifecycle_function(obj, obj.init_func_vaddr, false) ||
        !lifecycle_function(obj, obj.fini_func_vaddr, false) ||
        !lifecycle_array(obj, obj.init_array_vaddr, obj.init_array_size, false, false) ||
        !lifecycle_array(obj, obj.fini_array_vaddr, obj.fini_array_size, true, false)) return false;
  }
  return true;
}

void lifecycle_failure() {
  (void) UD_SYS_PrintV1("ELoader: invalid shared-library lifecycle callback\n");
  (void) UD_SYS_ExitV1(255);
  __builtin_trap();
}

void initialize_shared() {
  if (initialized_count) lifecycle_failure();
  for (uint32_t i = 0; i < initialization_count; ++i) {
    const auto &obj = objects[initialization_order[i]];
    if (!lifecycle_function(obj, obj.init_func_vaddr, true) ||
        !lifecycle_array(obj, obj.init_array_vaddr, obj.init_array_size, false, true)) lifecycle_failure();
    ++initialized_count;
  }
}

void finalize_shared() {
  while (initialized_count) {
    const auto &obj = objects[initialization_order[--initialized_count]];
    if (!lifecycle_array(obj, obj.fini_array_vaddr, obj.fini_array_size, true, true) ||
        !lifecycle_function(obj, obj.fini_func_vaddr, true)) lifecycle_failure();
  }
}

} // namespace

// ============================================================================
// Loader Entry Point
// ============================================================================

extern "C" [[noreturn]] void loader_main(const char *exec_path, uint32_t *initial_stack, HANDLE startup_event) {
  const char *failure = "ELoader: invalid executable path\n";
  if (!exec_path) {
    goto fail;
  }

  for (uint32_t i = 0; i < sizeof(objects[0].path); ++i) {
    objects[0].path[i] = exec_path[i];
    if (!exec_path[i]) {
      break;
    }
    if (i == sizeof(objects[0].path) - 1) {
      goto fail;
    }
  }

  object_count = 1;
  failure = "ELoader: failed to map executable\n";
  if (!load_object(objects[0]) || objects[0].type != ET_EXEC) {
    failure = load_failure_reason;
    goto fail;
  }

  // Discover and load all transitive shared library dependencies (breadth-first traversal)
  failure = "ELoader: failed to load dependency\n";
  for (uint32_t i = 0; i < object_count; ++i) {
    const LoadedObject &obj = objects[i];
    for (uint32_t j = 0; j < obj.needed_count; ++j) {
      const char *lib_name = nullptr;
      if (!get_string_at(obj, obj.needed_offsets[j], lib_name) ||
          !find_and_load_dependency(lib_name, exec_path, objects[i].dependencies[j])) {
        goto fail;
      }
    }
  }

  // Pass 1: Apply standard relocations to all loaded images
  for (uint32_t i = 0; i < object_count; ++i) {
    LoadedObject &obj = objects[i];
    failure = "ELoader: failed to relocate image\n";
    if (!relocate_table(obj, obj.rel_vaddr, obj.rel_size, false) ||
        (obj.plt_rel_size > 0 && (obj.plt_rel_type != DT_REL || !relocate_table(obj, obj.plt_rel_vaddr, obj.plt_rel_size, false)))) {
      goto fail;
    }
  }

  // Pass 2: Apply COPY relocations to the main executable
  failure = "ELoader: failed to relocate copy symbols\n";
  if (!relocate_table(objects[0], objects[0].rel_vaddr, objects[0].rel_size, true) ||
      !relocate_table(objects[0], objects[0].plt_rel_vaddr, objects[0].plt_rel_size, true)) {
    goto fail;
  }

  failure = "ELoader: cyclic dependency or invalid lifecycle metadata\n";
  if (!visit_dependencies(0) || !validate_lifecycle()) goto fail;

  {
    auto *info = reinterpret_cast<UnDOSStartupInfoV1 *>(initial_stack[1]);
    if (!info || !undos::abi::valid_startup(*info)) {
      failure = "ELoader: invalid startup ABI\n";
      goto fail;
    }
    info->initialize_shared = reinterpret_cast<uint32_t>(&initialize_shared);
    info->finalize_shared = reinterpret_cast<uint32_t>(&finalize_shared);
  }

  // Resolve and map all images before reporting launch success. Constructors
  // run as application code after this handshake, so they cannot stall the
  // creator or masquerade as a loader failure.
  failure = "ELoader: startup notification failed\n";
  if (UD_SYS_SetEventV1(startup_event) != SyscallReturnCode_OKAY) goto fail;
  (void) UD_SYS_CloseV1(startup_event);

  // The CRT initializes its streams before invoking shared-library constructors.
  initial_stack[2] = 0;

  // Pass through argc, argv and strings without touching their storage.
  asm volatile("movl %0, %%esp; jmp *%1" :: "r"(initial_stack), "r"(objects[0].entry + objects[0].bias) : "memory");
  __builtin_unreachable();

fail:
  (void) UD_SYS_PrintV1(failure);
  (void) UD_SYS_ExitV1(255);
  __builtin_unreachable();
}
