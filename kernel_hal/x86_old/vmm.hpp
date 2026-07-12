
#pragma once

#include <stdint.h>

namespace hal::x86 {
enum class PageFlags : uint32_t {
  NONE = 0,
  PRESENT = 1 << 0,      // Page is loaded in physical memory
  WRITABLE = 1 << 1,     // Read/Write if set, Read-Only if cleared
  USER = 1 << 2,         // User-mode (Ring 3) access if set, Kernel-only if cleared
  WRITE_THROUGH = 1 << 3,// Disable write-back caching for this page
  CACHE_DISABLE = 1 << 4,// Disable caching entirely for this page
  ACCESSED = 1 << 5,     // Set by CPU when the page is read or written
  DIRTY = 1 << 6,        // (PTE only) Set by CPU when the page is written to
  PAGE_SIZE_4M = 1 << 7, // (PDE only) If set, this entry maps a direct 4MB page frame
  GLOBAL = 1 << 8        // (PTE only) Prevents TLB flush on CR3 reload (requires CR4.PGE)
};

constexpr PageFlags operator|(PageFlags a, PageFlags b) noexcept { return static_cast<PageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }
constexpr PageFlags operator|(uint32_t a, PageFlags b) noexcept { return static_cast<PageFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b)); }
constexpr PageFlags operator|(PageFlags a, uint32_t b) noexcept { return static_cast<PageFlags>(static_cast<uint32_t>(a) | b); }
constexpr PageFlags operator&(PageFlags a, PageFlags b) noexcept { return static_cast<PageFlags>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b)); }
constexpr bool has_flag(PageFlags mask, PageFlags flag) noexcept { return (mask & flag) == flag; }

struct page_table_entry_t {
  // Physical addresses must be 4KB aligned; ensure we don't bleed into the flags space
  uint32_t value;
  constexpr page_table_entry_t() noexcept : value(0) {}
  constexpr page_table_entry_t(uint32_t physical_address, PageFlags flags) noexcept : value((physical_address & 0xFFFFF000) | static_cast<uint32_t>(flags)) {}
  constexpr void set_frame(uint32_t physical_address, PageFlags flags) noexcept { value = (physical_address & 0xFFFFF000) | static_cast<uint32_t>(flags); }
  [[nodiscard]] constexpr uint32_t get_frame() const noexcept { return value & 0xFFFFF000; }
  [[nodiscard]] constexpr bool is_present() const noexcept { return (value & static_cast<uint32_t>(PageFlags::PRESENT)) != 0; }
  constexpr void clear() noexcept { value = 0; }
};

struct page_directory_entry_t {
  uint32_t value;
  constexpr page_directory_entry_t() noexcept : value(0) {}
  constexpr page_directory_entry_t(uint32_t pt_physical_address, PageFlags flags) noexcept : value((pt_physical_address & 0xFFFFF000) | static_cast<uint32_t>(flags)) {}
  constexpr void set_page_table(uint32_t pt_physical_address, PageFlags flags) noexcept { value = (pt_physical_address & 0xFFFFF000) | static_cast<uint32_t>(flags); }
  [[nodiscard]] constexpr uint32_t get_page_table() const noexcept { return value & 0xFFFFF000; }
  [[nodiscard]] constexpr bool is_present() const noexcept { return (value & static_cast<uint32_t>(PageFlags::PRESENT)) != 0; }
  constexpr void clear() noexcept { value = 0; }
};

static_assert(sizeof(page_table_entry_t) == 4);
static_assert(sizeof(page_directory_entry_t) == 4);


struct [[gnu::aligned(4096)]] page_table_t {
  page_table_entry_t entries[1024];
};

struct [[gnu::aligned(4096)]] page_directory_t {
  page_directory_entry_t entries[1024];
};

static_assert(sizeof(page_table_t) == 4096);
static_assert(sizeof(page_directory_t) == 4096);

void init_vmm() noexcept;

}// namespace hal::x86
