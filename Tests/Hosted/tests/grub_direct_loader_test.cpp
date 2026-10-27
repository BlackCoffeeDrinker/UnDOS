#include <catch2/catch_test_macros.hpp>

extern "C" {
#include "../../../Boot/GrubDirect/core/cpu_profile.h"
#include "../../../Boot/GrubDirect/core/diagnostics.h"
#include "../../../Boot/GrubDirect/core/loader_core.h"
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
void put16(std::vector<std::uint8_t> &bytes, std::size_t offset,
           std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value);
  bytes[offset + 1] = static_cast<std::uint8_t>(value >> 8);
}

void put32(std::vector<std::uint8_t> &bytes, std::size_t offset,
           std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i)
    bytes[offset + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

std::vector<std::uint8_t> make_image(std::uint32_t base,
                                     const std::string &symbol_name = "_start",
                                     const std::string &undefined_symbol = {},
                                     std::uint32_t relocation_type = 2,
                                     std::uint32_t addend = 0,
                                     bool include_got = false) {
  constexpr std::size_t program_offset = 52;
  constexpr std::size_t text_offset = 0x1000;
  constexpr std::size_t strings_offset = 0x1004;
  constexpr std::size_t symbols_offset = 0x1040;
  constexpr std::size_t relocations_offset = 0x1080;
  constexpr std::size_t symbol_names_offset = 0x1100;
  constexpr std::size_t sections_offset = 0x1200;
  constexpr char section_names_data[] =
      "\0.text\0.shstrtab\0.symtab\0.strtab\0.rel.text\0.got\0";
  const std::string section_names(section_names_data,
                                  sizeof(section_names_data) - 1);
  std::string symbol_names(1, '\0');
  symbol_names += symbol_name;
  symbol_names.push_back('\0');
  if (!undefined_symbol.empty()) {
    symbol_names += undefined_symbol;
    symbol_names.push_back('\0');
  }
  const std::size_t got_symbol_name_offset = symbol_names.size();
  if (include_got) {
    symbol_names += "_GLOBAL_OFFSET_TABLE_";
    symbol_names.push_back('\0');
  }
  const std::size_t section_count =
      include_got ? 7 : (undefined_symbol.empty() ? 5 : 6);
  const std::size_t symbol_count =
      include_got ? 4 : (undefined_symbol.empty() ? 2 : 3);
  std::vector<std::uint8_t> bytes(sections_offset + section_count * 40, 0);

  bytes[0] = 0x7f;
  bytes[1] = 'E';
  bytes[2] = 'L';
  bytes[3] = 'F';
  bytes[4] = 1;
  bytes[5] = 1;
  bytes[6] = 1;
  put16(bytes, 16, 2);
  put16(bytes, 18, 3);
  put32(bytes, 20, 1);
  put32(bytes, 24, base);
  put32(bytes, 28, program_offset);
  put32(bytes, 32, sections_offset);
  put16(bytes, 40, 52);
  put16(bytes, 42, 32);
  put16(bytes, 44, 1);
  put16(bytes, 46, 40);
  put16(bytes, 48, static_cast<std::uint16_t>(section_count));
  put16(bytes, 50, 2);

  put32(bytes, program_offset, 1);
  put32(bytes, program_offset + 4, text_offset);
  put32(bytes, program_offset + 8, base);
  put32(bytes, program_offset + 16, 4);
  put32(bytes, program_offset + 20, 0x1000);
  put32(bytes, program_offset + 24, 5);
  put32(bytes, program_offset + 28, 0x1000);
  if (undefined_symbol.empty()) {
    bytes[text_offset] = 0x90;
    bytes[text_offset + 1] = 0x90;
    bytes[text_offset + 2] = 0x90;
    bytes[text_offset + 3] = 0xc3;
  } else {
    put32(bytes, text_offset, addend);
    put32(bytes, relocations_offset, base);
    put32(bytes, relocations_offset + 4,
          (2u << 8) | (relocation_type & 0xffu));
  }
  std::memcpy(bytes.data() + strings_offset, section_names.data(),
              section_names.size());
  put32(bytes, symbols_offset + 16, 1);
  put32(bytes, symbols_offset + 20, base);
  put32(bytes, symbols_offset + 24, 4);
  bytes[symbols_offset + 28] = 0x12;
  put16(bytes, symbols_offset + 30, 1);
  if (!undefined_symbol.empty()) {
    put32(bytes, symbols_offset + 32,
          static_cast<std::uint32_t>(1 + symbol_name.size() + 1));
    bytes[symbols_offset + 44] = 0x12;
  }
  if (include_got) {
    put32(bytes, symbols_offset + 48,
          static_cast<std::uint32_t>(got_symbol_name_offset));
    put32(bytes, symbols_offset + 52, base + 4);
    bytes[symbols_offset + 60] = 0x10;
    put16(bytes, symbols_offset + 62, 6);
  }
  std::memcpy(bytes.data() + symbol_names_offset, symbol_names.data(),
              symbol_names.size());

  const auto write_section = [&](std::size_t index, std::uint32_t name,
                                 std::uint32_t type, std::uint32_t flags,
                                 std::uint32_t address, std::uint32_t offset,
                                 std::uint32_t size, std::uint32_t link,
                                 std::uint32_t info, std::uint32_t alignment,
                                 std::uint32_t entry_size) {
    const std::size_t at = sections_offset + index * 40;
    put32(bytes, at, name);
    put32(bytes, at + 4, type);
    put32(bytes, at + 8, flags);
    put32(bytes, at + 12, address);
    put32(bytes, at + 16, offset);
    put32(bytes, at + 20, size);
    put32(bytes, at + 24, link);
    put32(bytes, at + 28, info);
    put32(bytes, at + 32, alignment);
    put32(bytes, at + 36, entry_size);
  };
  const auto name_offset = [&](const char *name) {
    const auto position = section_names.find(name);
    REQUIRE(position != std::string::npos);
    return static_cast<std::uint32_t>(position);
  };
  write_section(1, name_offset(".text"), 1, 2, base, text_offset, 4,
                0, 0, 16, 0);
  write_section(2, name_offset(".shstrtab"), 3, 0, 0, strings_offset,
                static_cast<std::uint32_t>(section_names.size()), 0, 0, 1, 0);
  write_section(3, name_offset(".symtab"), 2, 0, 0, symbols_offset,
                static_cast<std::uint32_t>(symbol_count * 16), 4, 1, 4, 16);
  write_section(4, name_offset(".strtab"), 3, 0, 0, symbol_names_offset,
                static_cast<std::uint32_t>(symbol_names.size()), 0, 0, 1, 0);
  if (!undefined_symbol.empty()) {
    write_section(5, name_offset(".rel.text"), 9, 0, 0,
                  relocations_offset, 8, 3, 1, 4, 8);
  }
  if (include_got) {
    write_section(6, name_offset(".got"), 8, 2, base + 4, 0, 8, 0, 0, 4, 0);
  }
  return bytes;
}

struct ImageFixture {
  std::array<udl_section, UDL_MAX_SECTIONS> sections{};
  udl_image image{};

  explicit ImageFixture(std::vector<std::uint8_t> &bytes, udl_u8 role) {
    REQUIRE(udl_image_open(&image, {bytes.data(), bytes.size()},
                           sections.data(), sections.size(), role) == UDL_OK);
  }
};
} // namespace

TEST_CASE("direct-loader validates and plans the current kernel/HAL ELF contract",
          "[boot][grub-direct]") {
  auto kernel_bytes = make_image(UDL_KERNEL_BASE);
  auto hal_bytes = make_image(UDL_HAL_LINK_BASE, "hal_init");
  ImageFixture kernel(kernel_bytes, UDL_ROLE_KERNEL);
  ImageFixture hal(hal_bytes, UDL_ROLE_HAL);
  udl_layout layout{};

  REQUIRE(udl_plan_layout(&kernel.image, &hal.image, 59060, &layout) == UDL_OK);
  CHECK(layout.kernel_end == 0xc0001000);
  CHECK(layout.hal_base == 0xc0001000);
  CHECK(layout.hal_end == 0xc0002000);
  CHECK(layout.stack_top - layout.stack_bottom == 16384);
  CHECK(layout.bootinfo_base == layout.stack_top);
  CHECK(layout.bootinfo_end % UDL_PAGE_SIZE == 0);
  CHECK(layout.kernel_entry == UDL_KERNEL_BASE);

  std::array<udl_export, UDL_MAX_EXPORTS> exports{};
  std::array<char, UDL_NAME_POOL_SIZE> names{};
  udl_export_table table{exports.data(), exports.size(), 0, names.data(),
                         names.size(), 0, 0, 0};
  REQUIRE(udl_collect_exports(&kernel.image, &hal.image, &table) == UDL_OK);
  CHECK(table.export_count == 2);
  CHECK(table.kernel_export_count == 1);
  CHECK(std::string(table.exports[0].name) == "_start");
  CHECK(table.exports[0].address == UDL_KERNEL_BASE);
  CHECK(table.exports[1].address == layout.hal_base);
  CHECK(std::string(table.exports[1].name) == "hal_init");

  std::array<std::uint8_t, 4096> kernel_destination{};
  std::array<std::uint8_t, 4096> hal_destination{};
  std::array<udl_write_slot, 8> writes{};
  CHECK(udl_link_images(&kernel.image, &hal.image, &table,
                       kernel_destination.data(), kernel_destination.size(),
                       hal_destination.data(), hal_destination.size(),
                       writes.data(), writes.size()) == UDL_OK);
  CHECK(kernel_destination[0] == 0x90);
  CHECK(kernel_destination[3] == 0xc3);
  CHECK(std::all_of(kernel_destination.begin() + 4,
                    kernel_destination.end(),
                    [](auto byte) { return byte == 0; }));
}

TEST_CASE("direct-loader diagnostic STOP names and VGA profile are exact",
          "[boot][grub-direct][diagnostics]") {
  const std::array<const char *, UDL_DIAG_STOP_COUNT> stops = {
      "command", "loaded", "callback", "profile", "prepared", "copied",
      "parameters", "segments", "cleanup", "paging", "frame", "jump"};
  for (std::size_t index = 0; index < stops.size(); ++index) {
    CHECK(udl_diag_stop_from_name(stops[index]) == index + 1);
    CHECK(std::string(udl_diag_names[index]) == stops[index]);
  }
  CHECK(udl_diag_stop_from_name(nullptr) == UDL_DIAG_STOP_INVALID);
  CHECK(udl_diag_stop_from_name("") == UDL_DIAG_STOP_INVALID);
  CHECK(udl_diag_stop_from_name("Command") == UDL_DIAG_STOP_INVALID);
  CHECK(udl_diag_stop_from_name("command-extra") == UDL_DIAG_STOP_INVALID);
  CHECK(udl_diag_stop_from_name("kernel") == UDL_DIAG_STOP_INVALID);

  CHECK(udl_diag_video_profile_supported(3, 80, 0));
  CHECK_FALSE(udl_diag_video_profile_supported(7, 80, 0));
  CHECK_FALSE(udl_diag_video_profile_supported(3, 40, 0));
  CHECK_FALSE(udl_diag_video_profile_supported(3, 80, 1));
}

TEST_CASE("direct-loader selects only CPUID-proven CR4 cleanup bits",
          "[boot][grub-direct][cpu]") {
  udl_u32 mask = 0xffffffffu;

  REQUIRE(udl_cpu_cleanup_mask(0, 0, 0xffffffffu, &mask) == UDL_OK);
  CHECK(mask == 0);

  REQUIRE(udl_cpu_cleanup_mask(1, 0, 0xffffffffu, &mask) == UDL_OK);
  CHECK(mask == 0);

  REQUIRE(udl_cpu_cleanup_mask(1, 1, 0, &mask) == UDL_OK);
  CHECK(mask == 0);

  REQUIRE(udl_cpu_cleanup_mask(1, 1, UDL_CPU_FEATURE_PSE, &mask) == UDL_OK);
  CHECK(mask == UDL_CR4_PSE);

  REQUIRE(udl_cpu_cleanup_mask(1, 1, UDL_CPU_FEATURE_PAE, &mask) == UDL_OK);
  CHECK(mask == UDL_CR4_PAE);

  REQUIRE(udl_cpu_cleanup_mask(1, 1,
                              UDL_CPU_FEATURE_PSE | UDL_CPU_FEATURE_PAE,
                              &mask) == UDL_OK);
  CHECK(mask == (UDL_CR4_PSE | UDL_CR4_PAE));

  REQUIRE(udl_cpu_cleanup_mask(1, 1, 1u << 31, &mask) == UDL_OK);
  CHECK(mask == 0);

  CHECK(udl_cpu_cleanup_mask(2, 1, 0, &mask) == UDL_BAD_ARGUMENT);
  CHECK(udl_cpu_cleanup_mask(1, 1, 0, nullptr) == UDL_BAD_ARGUMENT);
}

TEST_CASE("direct-loader applies cross-image i386 PC-relative relocations",
          "[boot][grub-direct]") {
  constexpr std::uint32_t kernel_addend = 0x24;
  constexpr std::uint32_t hal_addend = 0xfffffffcu;
  auto kernel_bytes =
      make_image(UDL_KERNEL_BASE, "_start", "hal_init", 2, kernel_addend);
  auto hal_bytes =
      make_image(UDL_HAL_LINK_BASE, "hal_init", "_start", 2, hal_addend);
  ImageFixture kernel(kernel_bytes, UDL_ROLE_KERNEL);
  ImageFixture hal(hal_bytes, UDL_ROLE_HAL);
  udl_layout layout{};

  REQUIRE(udl_plan_layout(&kernel.image, &hal.image, 59060, &layout) == UDL_OK);
  std::array<udl_export, UDL_MAX_EXPORTS> exports{};
  std::array<char, UDL_NAME_POOL_SIZE> names{};
  udl_export_table table{exports.data(), exports.size(), 0, names.data(),
                         names.size(), 0, 0, 0};
  REQUIRE(udl_collect_exports(&kernel.image, &hal.image, &table) == UDL_OK);

  std::array<std::uint8_t, 4096> kernel_destination{};
  std::array<std::uint8_t, 4096> hal_destination{};
  std::array<udl_write_slot, 8> writes{};
  REQUIRE(udl_link_images(&kernel.image, &hal.image, &table,
                          kernel_destination.data(), kernel_destination.size(),
                          hal_destination.data(), hal_destination.size(),
                          writes.data(), writes.size()) == UDL_OK);

  const auto read_word = [](const auto &bytes) {
    return static_cast<std::uint32_t>(bytes[0]) |
           (static_cast<std::uint32_t>(bytes[1]) << 8) |
           (static_cast<std::uint32_t>(bytes[2]) << 16) |
           (static_cast<std::uint32_t>(bytes[3]) << 24);
  };
  const std::uint32_t hal_bias = layout.hal_base - UDL_HAL_LINK_BASE;
  CHECK(read_word(kernel_destination) == layout.hal_base + kernel_addend);
  CHECK(read_word(hal_destination) ==
        UDL_KERNEL_BASE + hal_addend - hal_bias);
}

TEST_CASE("direct-loader fills validated GOT32 and GOT32X slots",
          "[boot][grub-direct]") {
  for (const std::uint32_t type : {3u, 43u}) {
    auto kernel_bytes =
        make_image(UDL_KERNEL_BASE, "_start", "hal_init", type, 4, true);
    auto hal_bytes = make_image(UDL_HAL_LINK_BASE, "hal_init");
    ImageFixture kernel(kernel_bytes, UDL_ROLE_KERNEL);
    ImageFixture hal(hal_bytes, UDL_ROLE_HAL);
    udl_layout layout{};
    REQUIRE(udl_plan_layout(&kernel.image, &hal.image, 59060, &layout) ==
            UDL_OK);
    std::array<udl_export, UDL_MAX_EXPORTS> exports{};
    std::array<char, UDL_NAME_POOL_SIZE> names{};
    udl_export_table table{exports.data(), exports.size(), 0, names.data(),
                           names.size(), 0, 0, 0};
    REQUIRE(udl_collect_exports(&kernel.image, &hal.image, &table) == UDL_OK);
    std::array<std::uint8_t, 4096> kernel_destination{};
    std::array<std::uint8_t, 4096> hal_destination{};
    std::array<udl_write_slot, 8> writes{};
    REQUIRE(udl_link_images(&kernel.image, &hal.image, &table,
                           kernel_destination.data(),
                           kernel_destination.size(), hal_destination.data(),
                           hal_destination.size(), writes.data(),
                           writes.size()) == UDL_OK);
    const std::uint32_t got_operand =
        static_cast<std::uint32_t>(kernel_destination[0]) |
        (static_cast<std::uint32_t>(kernel_destination[1]) << 8) |
        (static_cast<std::uint32_t>(kernel_destination[2]) << 16) |
        (static_cast<std::uint32_t>(kernel_destination[3]) << 24);
    const std::uint32_t got_slot =
        static_cast<std::uint32_t>(kernel_destination[8]) |
        (static_cast<std::uint32_t>(kernel_destination[9]) << 8) |
        (static_cast<std::uint32_t>(kernel_destination[10]) << 16) |
        (static_cast<std::uint32_t>(kernel_destination[11]) << 24);
    CHECK(got_operand == 4);
    CHECK(got_slot == layout.hal_base);
  }
}

TEST_CASE("direct-loader rejects unresolved and unsupported relocations",
          "[boot][grub-direct]") {
  const auto link_fixture = [](std::vector<std::uint8_t> &kernel_bytes,
                               std::vector<std::uint8_t> &hal_bytes,
                               std::uint32_t &result,
                               std::uint8_t *guarded_kernel) {
    std::array<udl_section, UDL_MAX_SECTIONS> kernel_sections{};
    std::array<udl_section, UDL_MAX_SECTIONS> hal_sections{};
    udl_image kernel{};
    udl_image hal{};
    result = udl_image_open(&kernel, {kernel_bytes.data(), kernel_bytes.size()},
                            kernel_sections.data(), kernel_sections.size(),
                            UDL_ROLE_KERNEL);
    if (result != UDL_OK)
      return;
    result = udl_image_open(&hal, {hal_bytes.data(), hal_bytes.size()},
                            hal_sections.data(), hal_sections.size(),
                            UDL_ROLE_HAL);
    if (result != UDL_OK)
      return;
    udl_layout layout{};
    result = udl_plan_layout(&kernel, &hal, 59060, &layout);
    if (result != UDL_OK)
      return;
    std::array<udl_export, UDL_MAX_EXPORTS> exports{};
    std::array<char, UDL_NAME_POOL_SIZE> names{};
    udl_export_table table{exports.data(), exports.size(), 0, names.data(),
                           names.size(), 0, 0, 0};
    result = udl_collect_exports(&kernel, &hal, &table);
    if (result != UDL_OK)
      return;
    std::array<std::uint8_t, 4096> hal_destination{};
    std::array<udl_write_slot, 8> writes{};
    result = udl_link_images(&kernel, &hal, &table, guarded_kernel + 1, 4096,
                             hal_destination.data(), hal_destination.size(),
                             writes.data(), writes.size());
  };

  std::vector<std::uint8_t> hal_bytes = make_image(UDL_HAL_LINK_BASE, "hal_init");
  std::array<std::uint8_t, 4098> guarded_kernel{};
  guarded_kernel.front() = 0xa5;
  guarded_kernel.back() = 0x5a;
  auto unresolved =
      make_image(UDL_KERNEL_BASE, "_start", "missing_symbol", 1, 0);
  std::uint32_t result = UDL_OK;
  link_fixture(unresolved, hal_bytes, result, guarded_kernel.data());
  CHECK(result == UDL_UNRESOLVED_SYMBOL);
  CHECK(guarded_kernel.front() == 0xa5);
  CHECK(guarded_kernel.back() == 0x5a);

  guarded_kernel.fill(0);
  guarded_kernel.front() = 0xa5;
  guarded_kernel.back() = 0x5a;
  auto unsupported =
      make_image(UDL_KERNEL_BASE, "_start", "hal_init", 99, 0);
  link_fixture(unsupported, hal_bytes, result, guarded_kernel.data());
  CHECK(result == UDL_BAD_RELOCATION);
  CHECK(guarded_kernel.front() == 0xa5);
  CHECK(guarded_kernel.back() == 0x5a);

  guarded_kernel.fill(0);
  guarded_kernel.front() = 0xa5;
  guarded_kernel.back() = 0x5a;
  auto invalid_got =
      make_image(UDL_KERNEL_BASE, "_start", "hal_init", 3, 0xfffffffcu, true);
  link_fixture(invalid_got, hal_bytes, result, guarded_kernel.data());
  CHECK(result == UDL_BAD_RELOCATION);
  CHECK(guarded_kernel.front() == 0xa5);
  CHECK(guarded_kernel.back() == 0x5a);
}

TEST_CASE("direct-loader keeps the first kernel export on duplicate names",
          "[boot][grub-direct]") {
  auto kernel_bytes = make_image(UDL_KERNEL_BASE);
  auto hal_bytes = make_image(UDL_HAL_LINK_BASE);
  ImageFixture kernel(kernel_bytes, UDL_ROLE_KERNEL);
  ImageFixture hal(hal_bytes, UDL_ROLE_HAL);
  std::array<udl_export, UDL_MAX_EXPORTS> exports{};
  std::array<char, UDL_NAME_POOL_SIZE> names{};
  udl_export_table table{exports.data(), exports.size(), 0, names.data(),
                         names.size(), 0, 0, 0};

  REQUIRE(udl_collect_exports(&kernel.image, &hal.image, &table) == UDL_OK);
  CHECK(table.export_count == 1);
  CHECK(table.kernel_export_count == 1);
  CHECK(table.duplicate_exports == 1);
  CHECK(table.exports[0].address == UDL_KERNEL_BASE);
}

TEST_CASE("direct-loader rejects truncated and unsupported ELF inputs",
          "[boot][grub-direct]") {
  auto bytes = make_image(UDL_KERNEL_BASE);
  std::array<udl_section, UDL_MAX_SECTIONS> sections{};
  udl_image image{};
  CHECK(udl_image_open(&image, {bytes.data(), 40}, sections.data(),
                       sections.size(), UDL_ROLE_KERNEL) == UDL_BAD_ELF);

  auto truncated = bytes;
  put32(truncated, 32, 0xfffffff0u);
  CHECK(udl_image_open(&image, {truncated.data(), truncated.size()},
                       sections.data(), sections.size(),
                       UDL_ROLE_KERNEL) == UDL_CAPACITY);

  auto unsupported = bytes;
  unsupported[4] = 2;
  CHECK(udl_image_open(&image, {unsupported.data(), unsupported.size()},
                       sections.data(), sections.size(),
                       UDL_ROLE_KERNEL) == UDL_UNSUPPORTED_ELF);

  auto invalid_program = bytes;
  put32(invalid_program, 52 + 16, 0x1001);
  CHECK(udl_image_open(&image, {invalid_program.data(), invalid_program.size()},
                       sections.data(), sections.size(),
                       UDL_ROLE_KERNEL) == UDL_BAD_ELF);

  auto invalid_names = bytes;
  put16(invalid_names, 50, 99);
  CHECK(udl_image_open(&image, {invalid_names.data(), invalid_names.size()},
                       sections.data(), sections.size(),
                       UDL_ROLE_KERNEL) == UDL_BAD_ELF);

  auto invalid_symbol_table = bytes;
  put32(invalid_symbol_table, 0x1200 + 3 * 40 + 36, 8);
  CHECK(udl_image_open(&image, {invalid_symbol_table.data(),
                                invalid_symbol_table.size()},
                       sections.data(), sections.size(),
                       UDL_ROLE_KERNEL) == UDL_BAD_ELF);

  auto oversized_segment = bytes;
  put32(oversized_segment, 52 + 16, 0x1001);
  CHECK(udl_image_open(&image, {oversized_segment.data(),
                                oversized_segment.size()},
                       sections.data(), sections.size(),
                       UDL_ROLE_KERNEL) == UDL_BAD_ELF);

  auto unterminated_symbol = bytes;
  unterminated_symbol[0x1107] = 'x';
  CHECK(udl_image_open(&image, {unterminated_symbol.data(),
                                unterminated_symbol.size()},
                       sections.data(), sections.size(),
                       UDL_ROLE_KERNEL) == UDL_BAD_ELF);

  auto malformed_relocation =
      make_image(UDL_KERNEL_BASE, "_start", "hal_init", 2, 0);
  put32(malformed_relocation, 0x1080, UDL_KERNEL_BASE + 1);
  CHECK(udl_image_open(&image, {malformed_relocation.data(),
                                malformed_relocation.size()},
                       sections.data(), sections.size(),
                       UDL_ROLE_KERNEL) == UDL_BAD_ELF);
}

TEST_CASE("direct-loader normalizes memory maps conservatively",
          "[boot][grub-direct]") {
  const std::array<udl_raw_memory_region, 4> raw{{
      {0, 64ull * 1024 * 1024, UDL_GRUB_MEMORY_AVAILABLE},
      {3ull * 1024 * 1024, 1024ull * 1024, UDL_GRUB_MEMORY_ACPI},
      {2ull * 1024 * 1024, 1024ull * 1024, 2},
      {0, 0, UDL_GRUB_MEMORY_AVAILABLE},
  }};
  udl_memory_map map{};
  REQUIRE(udl_normalize_memory_map(raw.data(), raw.size(), &map) == UDL_OK);
  REQUIRE(map.count == 5);
  CHECK(map.regions[0].base == 0);
  CHECK(map.regions[0].length == 1024 * 1024);
  CHECK(map.regions[0].type == UDL_MEMORY_RESERVED);
  CHECK(map.regions[1].base == 1024 * 1024);
  CHECK(map.regions[1].type == UDL_MEMORY_AVAILABLE);
  CHECK(map.regions[2].base == 2 * 1024 * 1024);
  CHECK(map.regions[2].type == UDL_MEMORY_RESERVED);
  CHECK(map.regions[3].base == 3 * 1024 * 1024);
  CHECK(map.regions[3].type == UDL_MEMORY_ACPI_RECLAIM);
  CHECK(map.regions[4].base == 4 * 1024 * 1024);
  CHECK(map.regions[4].type == UDL_MEMORY_AVAILABLE);

  udl_u32 base = 0;
  REQUIRE(udl_choose_physical_base(&map, 2 * 1024 * 1024, &base) == UDL_OK);
  CHECK(base == 16 * 1024 * 1024);
}

TEST_CASE("direct-loader rejects wrapping maps and reports no-fit placement",
          "[boot][grub-direct]") {
  const udl_raw_memory_region wrapping{
      0xfffffffffffff000ull, 0x2000, UDL_GRUB_MEMORY_AVAILABLE};
  udl_memory_map map{};
  CHECK(udl_normalize_memory_map(&wrapping, 1, &map) == UDL_BAD_MEMORY_MAP);

  const udl_raw_memory_region small{
      1024 * 1024, 15ull * 1024 * 1024, UDL_GRUB_MEMORY_AVAILABLE};
  REQUIRE(udl_normalize_memory_map(&small, 1, &map) == UDL_OK);
  udl_u32 base = 0;
  CHECK(udl_choose_physical_base(&map, 16 * 1024 * 1024, &base)
        == UDL_NO_MEMORY);
}

TEST_CASE("direct-loader clips memory maps and ignores addresses above its limit",
          "[boot][grub-direct]") {
  const std::array<udl_raw_memory_region, 4> raw{{
      {0x1f000000ull, 64ull * 1024 * 1024, UDL_GRUB_MEMORY_AVAILABLE},
      {0x100000000ull, 1024ull * 1024, UDL_GRUB_MEMORY_AVAILABLE},
      {0, 2ull * 1024 * 1024, UDL_GRUB_MEMORY_AVAILABLE},
      {0x100001, 0x1000, UDL_GRUB_MEMORY_ACPI},
  }};
  udl_memory_map map{};

  REQUIRE(udl_normalize_memory_map(raw.data(), raw.size(), &map) == UDL_OK);
  REQUIRE(map.count == 4);
  CHECK(map.regions[0].type == UDL_MEMORY_RESERVED);
  CHECK(map.regions[0].base == 0);
  CHECK(map.regions[0].length == 1024 * 1024);
  CHECK(map.regions[1].type == UDL_MEMORY_ACPI_RECLAIM);
  CHECK(map.regions[1].base == 1024 * 1024);
  CHECK(map.regions[1].length == 2 * UDL_PAGE_SIZE);
  CHECK(map.regions[2].type == UDL_MEMORY_AVAILABLE);
  CHECK(map.regions[2].base == 1024 * 1024 + 2 * UDL_PAGE_SIZE);
  CHECK(map.regions[2].length == 1024 * 1024 - 2 * UDL_PAGE_SIZE);
  CHECK(map.regions[3].type == UDL_MEMORY_AVAILABLE);
  CHECK(map.regions[3].base == 0x1f000000u);
  CHECK(map.regions[3].length == 16 * 1024 * 1024);
  CHECK((udl_u64) map.regions[3].base + map.regions[3].length
        == 512ull * 1024 * 1024);
}

TEST_CASE("direct-loader selects alternate high placement and rejects fragmentation",
          "[boot][grub-direct]") {
  const std::array<udl_raw_memory_region, 2> alternate{{
      {1024 * 1024ull, 7 * 1024 * 1024ull, UDL_GRUB_MEMORY_AVAILABLE},
      {32 * 1024 * 1024ull, 16 * 1024 * 1024ull,
       UDL_GRUB_MEMORY_AVAILABLE},
  }};
  udl_memory_map map{};
  REQUIRE(udl_normalize_memory_map(alternate.data(), alternate.size(), &map) ==
          UDL_OK);
  udl_u32 base = 0;
  REQUIRE(udl_choose_physical_base(&map, 2 * 1024 * 1024, &base) == UDL_OK);
  CHECK(base == 46 * 1024 * 1024);
  CHECK(base != 16 * 1024 * 1024);

  std::array<udl_raw_memory_region, 33> fragmented{};
  for (std::size_t i = 0; i < fragmented.size(); ++i) {
    fragmented[i] = {1024 * 1024ull + i * 2 * UDL_PAGE_SIZE,
                     UDL_PAGE_SIZE, UDL_GRUB_MEMORY_AVAILABLE};
  }
  CHECK(udl_normalize_memory_map(fragmented.data(), fragmented.size(), &map) ==
        UDL_CAPACITY);
}
