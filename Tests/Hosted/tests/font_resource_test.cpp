#include "../../../Apps/Fontloader/font_load_result.hpp"
#include "gfx/font/default_font.hpp"
#include "syscalls/KErrorToSysReturnCode.hpp"
#include "ob_test_support.hpp"
#include "vmm.hpp"
#include "gfx/font/face.hpp"
#include <catch2/catch_test_macros.hpp>
#include <kernel/gfx/KFontResourceTable.hpp>
#include <undos/font/resource.hpp>
#include <vector.hpp>

namespace {
using namespace undos::font;

kstd::vector<uint8_t> resource(uint32_t programSize = 1) {
  const uint32_t sizes[] = {3 * GlyphSize, 3 * PointSize, 4, ComponentSize, 2 * CmapSize, 4, programSize, 0, 0, LimitsSize};
  uint32_t size = PayloadStart;
  for (const auto length : sizes) size += aligned(length);
  kstd::vector<uint8_t> bytes(size, 0);
  const uint32_t header[] = {Magic, Version, HeaderSize, size, SectionCount, 3, 2048, 1800,
                             static_cast<uint32_t>(-400), 0, 400, 0};
  for (unsigned i = 0; i < 12; ++i) write_u32(bytes.data() + i * 4, header[i]);
  uint32_t offset = PayloadStart;
  for (uint32_t i = 0; i < SectionCount; ++i) {
    auto *entry = bytes.data() + HeaderSize + i * DirectoryEntrySize;
    write_u32(entry, i + 1);
    write_u32(entry + 4, offset);
    write_u32(entry + 8, sizes[i]);
    offset += aligned(sizes[i]);
  }
  const ResourceView view({.data = bytes.data(), .size = bytes.size()});
  auto *glyphs = const_cast<uint8_t *>(view.section(Section::Glyphs).data);
  const uint32_t glyph[][15] = {
      {1, 1200, 0, 0, 0, 1000, 1000, 0, 3, 0, 1, 0, 0, 0, programSize},
      {0, 600, 0, 0, 0, 0, 0, 3, 0, 1, 0, 0, 0, programSize, 0},
      {2, 1200, 0, 0, 0, 1000, 1000, 3, 0, 1, 0, 0, 1, programSize, 0}};
  for (unsigned i = 0; i < 3; ++i)
    for (unsigned j = 0; j < 15; ++j) write_u32(glyphs + i * GlyphSize + j * 4, glyph[i][j]);
  auto *points = const_cast<uint8_t *>(view.section(Section::Points).data);
  const uint32_t coordinates[] = {0, 0, 1, 1000, 0, 1, 500, 1000, 1};
  for (unsigned i = 0; i < 9; ++i) write_u32(points + i * 4, coordinates[i]);
  write_u32(const_cast<uint8_t *>(view.section(Section::Contours).data), 2);
  auto *component = const_cast<uint8_t *>(view.section(Section::Components).data);
  const uint32_t fields[] = {0, 2, 0, 0, 16384, 0, 0, 16384};
  for (unsigned i = 0; i < 8; ++i) write_u32(component + i * 4, fields[i]);
  auto *cmap = const_cast<uint8_t *>(view.section(Section::Cmap).data);
  const uint32_t mappings[] = {32, 32, 1, 65, 65, 0};
  for (unsigned i = 0; i < 6; ++i) write_u32(cmap + i * 4, mappings[i]);
  auto *limits = const_cast<uint8_t *>(view.section(Section::Limits).data);
  const uint32_t requirements[] = {3, 1, 3, 1, 2, 0, 0, 0, 0, 1, programSize, 1, 1};
  for (unsigned i = 0; i < 13; ++i) write_u32(limits + i * 4, requirements[i]);
  return bytes;
}

void change(kstd::vector<uint8_t> &bytes, Section section, uint32_t offset, uint32_t value) {
  const ResourceView view({.data = bytes.data(), .size = bytes.size()});
  write_u32(const_cast<uint8_t *>(view.section(section).data) + offset, value);
}
}

TEST_CASE("Normalized fonts preserve points composites and raw programs", "[font][resource]") {
  const auto bytes = resource();
  REQUIRE(validate({bytes.data(), bytes.size()}) == Error::None);
  const ResourceView view({.data = bytes.data(), .size = bytes.size()});
  REQUIRE(view.section(Section::Glyphs).size == 180);
  REQUIRE(view.section(Section::GlyphPrograms).size == 1);
  REQUIRE(read_i32(bytes.data() + 32) == -400);
  REQUIRE(read_u32(view.section(Section::Components).data + 16) == 16384);
  REQUIRE(read_u32(view.section(Section::Contours).data) == 2);
}

TEST_CASE("Normalized font validation rejects every truncated resource", "[font][resource]") {
  const auto bytes = resource();
  REQUIRE(validate({nullptr, bytes.size()}) == Error::InvalidSize);
  for (size_t i = 0; i < bytes.size(); ++i)
    REQUIRE(validate({bytes.data(), i}) != Error::None);
}

TEST_CASE("Normalized font directory has canonical sizes offsets and padding", "[font][resource]") {
  auto bytes = resource();
  SECTION("version") { write_u32(bytes.data() + 4, 2); }
  SECTION("reserved") { write_u32(bytes.data() + 48, 1); }
  SECTION("overlap") { write_u32(bytes.data() + HeaderSize + 4, HeaderSize); }
  SECTION("huge section") { write_u32(bytes.data() + HeaderSize + 8, UINT32_MAX); }
  SECTION("duplicate type") { write_u32(bytes.data() + HeaderSize + DirectoryEntrySize, 1); }
  SECTION("nonzero padding") {
    const ResourceView view({.data = bytes.data(), .size = bytes.size()});
    const auto program = view.section(Section::GlyphPrograms);
    bytes[static_cast<size_t>(program.data - bytes.data()) + 1] = 1;
  }
  REQUIRE(validate({bytes.data(), bytes.size()}) != Error::None);
}

TEST_CASE("Normalized glyph validation enforces independent bounds", "[font][resource]") {
  auto bytes = resource();
  SECTION("point coordinate") { change(bytes, Section::Points, 0, 32768); }
  SECTION("on curve") { change(bytes, Section::Points, 8, 2); }
  SECTION("contour endpoint") { change(bytes, Section::Contours, 0, 3); }
  SECTION("unowned points") { change(bytes, Section::Glyphs, 28, 1); }
  SECTION("program range") { change(bytes, Section::Glyphs, 56, UINT32_MAX); }
  SECTION("glyph kind") { change(bytes, Section::Glyphs, 0, 3); }
  SECTION("too small maxp") { change(bytes, Section::Limits, 0, 2); }
  SECTION("invalid zones") { change(bytes, Section::Limits, 16, 0); }
  SECTION("invalid bbox") { change(bytes, Section::Glyphs, 12, 2000); }
  REQUIRE(validate({bytes.data(), bytes.size()}) != Error::None);
}

TEST_CASE("Normalized fonts reject cyclic composites and invalid attachment", "[font][resource]") {
  auto bytes = resource();
  SECTION("self cycle") { change(bytes, Section::Components, 0, 2); }
  SECTION("bad child") { change(bytes, Section::Components, 0, 3); }
  SECTION("first component cannot attach to absent parent point") {
    change(bytes, Section::Components, 4, 0);
  }
  SECTION("conflicting offset scaling") { change(bytes, Section::Components, 4, 0x1802); }
  SECTION("continuation flag") { change(bytes, Section::Components, 4, 0x22); }
  SECTION("out of range transform") { change(bytes, Section::Components, 16, 65536); }
  SECTION("underdeclared composite points") { change(bytes, Section::Limits, 8, 2); }
  SECTION("underdeclared component depth") { change(bytes, Section::Limits, 48, 0); }
  SECTION("transform without flag") { change(bytes, Section::Components, 16, 8192); }
  SECTION("byte argument overflow") { change(bytes, Section::Components, 8, 128); }
  REQUIRE(validate({bytes.data(), bytes.size()}) != Error::None);
}

TEST_CASE("Normalized cmap rejects invalid Unicode and glyph ranges", "[font][resource]") {
  auto bytes = resource();
  SECTION("surrogate") {
    change(bytes, Section::Cmap, 0, 0xd800);
    change(bytes, Section::Cmap, 4, 0xd800);
  }
  SECTION("non Unicode") { change(bytes, Section::Cmap, 4, 0x110000); }
  SECTION("reversed") { change(bytes, Section::Cmap, 0, 33); }
  SECTION("overlap") { change(bytes, Section::Cmap, 12, 32); }
  SECTION("glyph overflow") { change(bytes, Section::Cmap, 8, UINT32_MAX); }
  SECTION("sequential glyph range overflow") { change(bytes, Section::Cmap, 4, 40); }
  REQUIRE(validate({bytes.data(), bytes.size()}) != Error::None);
}

TEST_CASE("Font component depth accepts its exact ceiling and rejects overflow", "[font][resource]") {
  const auto original = resource();
  const ResourceView originalView({.data = original.data(), .size = original.size()});
  for (uint32_t depth = MaxComponentDepth; depth <= MaxComponentDepth + 1; ++depth) {
    const uint32_t sizes[] = {(depth + 1) * GlyphSize, 3 * PointSize, 4, depth * ComponentSize,
                             2 * CmapSize, 4, 1, 0, 0, LimitsSize};
    uint32_t offsets[SectionCount]{};
    uint32_t total = PayloadStart;
    for (uint32_t i = 0; i < SectionCount; ++i) {
      offsets[i] = total;
      total = aligned(total + sizes[i]);
    }
    kstd::vector<uint8_t> bytes(total, 0);
    for (uint32_t i = 0; i < HeaderSize; ++i) bytes[i] = original[i];
    write_u32(bytes.data() + 12, total);
    write_u32(bytes.data() + 20, depth + 1);
    for (uint32_t i = 0; i < SectionCount; ++i) {
      auto *directory = bytes.data() + HeaderSize + i * DirectoryEntrySize;
      write_u32(directory, i + 1);
      write_u32(directory + 4, offsets[i]);
      write_u32(directory + 8, sizes[i]);
      const auto section = originalView.section(static_cast<Section>(i + 1));
      if (i != 0 && i != 3)
        for (size_t j = 0; j < section.size; ++j) bytes[offsets[i] + j] = section.data[j];
    }
    const auto glyphs = originalView.section(Section::Glyphs);
    for (uint32_t i = 0; i < GlyphSize; ++i) bytes[offsets[0] + i] = glyphs.data[i];
    const auto component = originalView.section(Section::Components);
    for (uint32_t id = 1; id <= depth; ++id) {
      auto *g = bytes.data() + offsets[0] + id * GlyphSize;
      for (uint32_t i = 0; i < GlyphSize; ++i) g[i] = glyphs.data[2 * GlyphSize + i];
      write_u32(g + 44, id - 1);
      auto *c = bytes.data() + offsets[3] + (id - 1) * ComponentSize;
      for (uint32_t i = 0; i < ComponentSize; ++i) c[i] = component.data[i];
      write_u32(c, id - 1);
    }
    change(bytes, Section::Limits, 48, MaxComponentDepth);
    REQUIRE(validate({bytes.data(), bytes.size()}) ==
            (depth == MaxComponentDepth ? Error::None : Error::ResourceLimit));
  }
}

namespace {
// Adapts the expected-returning upload API to the status/out-parameter shape
// these tests assert on; failed calls report handle 0.
kstd::error_code BeginUpload(kernel::font::ResourceTable &table, kernel::font::ResourceBudget &budget, uint32_t size, uint32_t &handle) {
  EnsureObjectManagerInitialized();
  const auto result = table.begin(budget, size);
  handle = result ? *result : 0;
  return result ? kernel::KernelError::Success : result.error();
}

kernel::font::FaceRegistry &registry() {
  static kernel::font::FaceRegistry instance;
  return instance;
}

kstd::error_code CommitUpload(kernel::font::ResourceTable &table, uint32_t handle, uint32_t &face, uint32_t &validationError) {
  const auto result = table.commit(handle, registry(), validationError);
  face = result ? *result : 0;
  return result ? kernel::KernelError::Success : result.error();
}
}// namespace

TEST_CASE("Font uploads commit immutable resources with generation-safe handles", "[font][upload]") {
  using namespace kernel::font;
  ResourceBudget budget;
  ResourceTable table;
  auto bytes = resource();
  uint32_t upload = 99, face = 99, error = 99;
  REQUIRE_FALSE(BeginUpload(table, budget, static_cast<uint32_t>(bytes.size()), upload));
  REQUIRE(upload != 0);
  REQUIRE(table.bytes() == bytes.size());
  REQUIRE(CommitUpload(table, upload, face, error) == kernel::KernelError::InvalidState);
  REQUIRE(face == 0);
  REQUIRE(table.append(upload, 1, bytes.data(), 10) == kernel::KernelError::InvalidOffset);
  REQUIRE_FALSE(table.append(upload, 0, bytes.data(), 10));
  REQUIRE(table.append(upload, 0, bytes.data(), 10) == kernel::KernelError::InvalidOffset);
  REQUIRE_FALSE(table.append(upload, 10, bytes.data() + 10, bytes.size() - 10));
  // Mutating the producer after upload cannot change the committed snapshot.
  bytes[0] = 0;
  REQUIRE_FALSE(CommitUpload(table, upload, face, error));
  REQUIRE(error == 0);
  REQUIRE(face != upload);
  REQUIRE(table.registry_handle(upload) == 0);
  const uint32_t published = table.registry_handle(face);
  REQUIRE(published != 0);
  REQUIRE(budget.used() == 0);
  REQUIRE(registry().bytes() == bytes.size());
  auto held = registry().acquire(published);
  REQUIRE(held);
  REQUIRE(table.close(upload) == kernel::KernelError::InvalidHandle);
  REQUIRE(table.append(face, 0, bytes.data(), 1) == kernel::KernelError::InvalidState);
  REQUIRE(CommitUpload(table, face, upload, error) == kernel::KernelError::InvalidState);
  REQUIRE_FALSE(table.close(face));
  REQUIRE(table.close(face) == kernel::KernelError::InvalidHandle);
  REQUIRE(table.registry_handle(face) == 0);
  REQUIRE_FALSE(registry().acquire(published));
  REQUIRE(held->byteSize() == bytes.size());
  held.reset();
  REQUIRE(registry().bytes() == 0);
  REQUIRE(budget.used() == 0);
}

TEST_CASE("Invalid font commits discard staging without publishing", "[font][upload]") {
  using namespace kernel::font;
  ResourceBudget budget;
  ResourceTable table;
  auto bytes = resource();
  bytes[0] = 0;
  uint32_t upload = 0, face = 99, error = 99;
  REQUIRE_FALSE(BeginUpload(table, budget, static_cast<uint32_t>(bytes.size()), upload));
  REQUIRE_FALSE(table.append(upload, 0, bytes.data(), bytes.size()));
  REQUIRE(CommitUpload(table, upload, face, error) == kernel::KernelError::InvalidResource);
  REQUIRE(face == 0);
  REQUIRE(error == static_cast<uint32_t>(Error::InvalidHeader));
  REQUIRE(table.bytes() == 0);
  REQUIRE(table.close(upload) == kernel::KernelError::InvalidHandle);
  REQUIRE(budget.used() == 0);
}

TEST_CASE("Font upload quotas are aggregate across processes and cleanup", "[font][upload]") {
  using namespace kernel::font;
  ResourceBudget budget;
  uint32_t stale = 0;
  {
    ResourceTable first, second;
    uint32_t handle = 99;
    REQUIRE(BeginUpload(first, budget, 0, handle) == kernel::KernelError::InvalidSize);
    REQUIRE(handle == 0);
    REQUIRE_FALSE(BeginUpload(first, budget, undos::font::MaxResourceBytes, stale));
    REQUIRE(BeginUpload(second, budget, undos::font::PayloadStart, handle) == kernel::KernelError::ResourceLimitExceeded);
    REQUIRE(second.bytes() == 0);
    REQUIRE(first.append(stale, 0, nullptr, 4096) == kernel::KernelError::InvalidSize);
    REQUIRE(first.append(stale, UINT32_MAX, reinterpret_cast<const uint8_t *>("a"), 1) == kernel::KernelError::InvalidOffset);
    REQUIRE_FALSE(first.close(stale));
    REQUIRE_FALSE(BeginUpload(first, budget, undos::font::PayloadStart, handle));
    REQUIRE(handle != stale);
    REQUIRE(first.close(stale) == kernel::KernelError::InvalidHandle);
    REQUIRE_FALSE(BeginUpload(second, budget, undos::font::PayloadStart, stale));
  }
  REQUIRE(budget.used() == 0);
}

TEST_CASE("Font resource slots are bounded and explicit clear releases faces", "[font][upload]") {
  using namespace kernel::font;
  ResourceBudget budget;
  ResourceTable table;
  uint32_t handle = 0;
  for (uint32_t i = 0; i < ResourceSlots; ++i)
    REQUIRE_FALSE(BeginUpload(table, budget, PayloadStart, handle));
  REQUIRE(BeginUpload(table, budget, PayloadStart, handle) == kernel::KernelError::ResourceLimitExceeded);
  table.clear();
  REQUIRE(table.bytes() == 0);
  REQUIRE(budget.used() == 0);
}

TEST_CASE("Font uploads enforce the syscall boundary without partial writes", "[font][upload]") {
  using namespace kernel::font;
  ResourceBudget budget;
  ResourceTable table;
  const auto bytes = resource(5000);
  uint32_t upload = 0, face = 0, error = 0;
  REQUIRE_FALSE(BeginUpload(table, budget, static_cast<uint32_t>(bytes.size()), upload));
  REQUIRE(table.append(upload, 0, bytes.data(), 4097) == kernel::KernelError::InvalidSize);
  REQUIRE_FALSE(table.append(upload, 0, bytes.data(), 4096));
  REQUIRE(CommitUpload(table, upload, face, error) == kernel::KernelError::InvalidState);
  REQUIRE_FALSE(table.append(upload, 4096, bytes.data() + 4096, bytes.size() - 4096));
  REQUIRE_FALSE(CommitUpload(table, upload, face, error));
  REQUIRE(table.bytes() == bytes.size());
  table.clear();
  REQUIRE(table.bytes() == 0);
  REQUIRE(budget.used() == 0);
  REQUIRE(table.close(face) == kernel::KernelError::InvalidHandle);
}

namespace test_host_vmm {
void FailNextMalloc() noexcept;
}

TEST_CASE("Font staging OOM rolls back without publishing or advancing a slot", "[font][upload][oom]") {
  EnsureObjectManagerInitialized();
  using namespace kernel::font;
  ResourceBudget budget;
  ResourceTable table;
  constexpr uint32_t largeSize = 708992;
  const auto retained = table.begin(budget, PayloadStart);
  REQUIRE(retained);
  const auto before = budget.used();
  test_host_vmm::FailNextMalloc();
  const auto failed = table.begin(budget, largeSize);
  REQUIRE_FALSE(failed);
  REQUIRE(failed.error() == kernel::KernelError::OutOfMemory);
  REQUIRE(table.bytes() == before);
  REQUIRE(budget.used() == before);
  REQUIRE(table.registry_handle((1 << HandleSlotBits) | 2) == 0);
  REQUIRE(table.close((1 << HandleSlotBits) | 2) == kernel::KernelError::InvalidHandle);
  const auto retried = table.begin(budget, largeSize);
  REQUIRE(retried);
  REQUIRE(*retried == ((1 << HandleSlotBits) | 2));
  REQUIRE(budget.used() == before + largeSize);
  REQUIRE_FALSE(table.close(*retried));
  REQUIRE(budget.used() == before);
  table.clear();
  REQUIRE(table.bytes() == 0);
  REQUIRE(budget.used() == 0);
  table.clear();
  REQUIRE(budget.used() == 0);
}

TEST_CASE("Font storage moves transfer one allocation and release replaced ownership", "[font][upload]") {
  EnsureObjectManagerInitialized();
  using kernel::font::FontResourceStorage;
  constexpr size_t size = 1000;
  const auto before = kernel::vmm::debug_slab_cache_allocated_bytes(size);
  {
    auto first = FontResourceStorage::allocate(size);
    REQUIRE(first);
    auto *address = first.data();
    first.data()[0] = 31;
    auto moved = static_cast<FontResourceStorage &&>(first);
    REQUIRE_FALSE(first);
    REQUIRE(first.size() == 0);
    REQUIRE(moved.data() == address);
    REQUIRE(moved.data()[0] == 31);
    auto replacement = FontResourceStorage::allocate(size);
    REQUIRE(replacement);
    REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(size) == before + 2048);
    replacement = static_cast<FontResourceStorage &&>(moved);
    REQUIRE_FALSE(moved);
    REQUIRE(replacement.data() == address);
    REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(size) == before + 1024);
  }
  REQUIRE(kernel::vmm::debug_slab_cache_allocated_bytes(size) == before);
}

TEST_CASE("Font commit transfers storage without copying and clear retains external face ownership", "[font][upload]") {
  EnsureObjectManagerInitialized();
  using namespace kernel::font;
  ResourceBudget budget;
  FaceRegistry faces;
  ResourceTable table;
  const auto data = resource();
  const auto upload = table.begin(budget, uint32_t(data.size()));
  REQUIRE(upload);
  REQUIRE_FALSE(table.append(*upload, 0, data.data(), data.size()));
  uint32_t validation = 0;
  const auto committed = table.commit(*upload, faces, validation);
  REQUIRE(committed);
  auto held = faces.acquire(table.registry_handle(*committed));
  REQUIRE(held);
  const auto *address = held->view().bytes().data;
  REQUIRE(budget.used() == 0);
  REQUIRE(faces.bytes() == data.size());
  table.clear();
  REQUIRE(table.bytes() == 0);
  REQUIRE(budget.used() == 0);
  REQUIRE(faces.bytes() == data.size());
  REQUIRE(held->view().bytes().data == address);
  held.reset();
  REQUIRE(faces.bytes() == 0);
}

TEST_CASE("Committed face retains the exact moved font storage until its last reference", "[font][upload]") {
  EnsureObjectManagerInitialized();
  using namespace kernel::font;
  FaceRegistry faces;
  auto bytes = resource();
  auto storage = FontResourceStorage::allocate(bytes.size());
  REQUIRE(storage);
  for (size_t i = 0; i < bytes.size(); ++i) storage.data()[i] = bytes[i];
  auto *address = storage.data();
  uint32_t validation = 0;
  const auto published = faces.publish(static_cast<FontResourceStorage &&>(storage), validation);
  REQUIRE(published);
  REQUIRE_FALSE(storage);
  auto held = faces.acquire(*published);
  REQUIRE(held);
  REQUIRE(held->view().bytes().data == address);
  REQUIRE_FALSE(faces.close(*published));
  REQUIRE(faces.bytes() == bytes.size());
  held.reset();
  REQUIRE(faces.bytes() == 0);
}

TEST_CASE("Fontloader policy integrates actual resource rollback before builtin publication", "[font][upload][fallback]") {
  EnsureObjectManagerInitialized();
  using namespace kernel::font;
  using namespace fontloader;
  for (unsigned failure = 0; failure < 3; ++failure) {
    ResourceBudget budget;
    FaceRegistry faces;
    DefaultFont defaults(faces);
    ResourceTable table;
    auto bytes = resource();
    if (failure == 2) bytes[0] = 0;
    uint32_t regular = 0, bold = 0;
    const auto result = load_pair("regular", "bold", regular, bold,
        [&](const char *path, uint32_t &face) {
          if (path[0] == 'b') return LoadResult::ConfiguredFontNotFound;
          if (failure == 0) test_host_vmm::FailNextMalloc();
          return upload_resource(bytes.data(), uint32_t(bytes.size()), face,
              [&](uint32_t size, uint32_t &upload) -> uint32_t {
                const auto begun = table.begin(budget, size);
                if (begun) upload = *begun;
                return kernel::KErrorToSysReturnCode(begun ? kernel::make_error_code(kernel::KernelError::Success) : begun.error());
              },
              [&](uint32_t upload, uint32_t offset, const uint8_t *data, uint32_t size) -> uint32_t {
                return kernel::KErrorToSysReturnCode(table.append(upload, offset, data, size));
              },
              [&](uint32_t upload, uint32_t &output, uint32_t &validation) -> uint32_t {
                const auto committed = table.commit(upload, faces, validation);
                if (committed) output = *committed;
                return kernel::KErrorToSysReturnCode(committed ? kernel::make_error_code(kernel::KernelError::Success) : committed.error());
              },
              [&](uint32_t upload) -> uint32_t { return kernel::KErrorToSysReturnCode(table.close(upload)); },
              [](const char *, uint32_t) {});
        }, [&](uint32_t handle) { return !table.close(handle); });
    REQUIRE(result == (failure == 0 ? LoadResult::UploadOutOfMemory :
                       failure == 1 ? LoadResult::ConfiguredFontNotFound : LoadResult::Fatal));
    REQUIRE(table.bytes() == 0);
    REQUIRE(budget.used() == 0);
    REQUIRE(faces.bytes() == 0);
    const bool ready = finish_startup(uint32_t(result), [&] {
      REQUIRE(table.bytes() == 0);
      REQUIRE(faces.bytes() == 0);
      return !defaults.publish_builtin([](void *, DefaultFont &target, UiFont &&candidate) {
        UiFont displaced;
        target.commit(static_cast<UiFont &&>(candidate), displaced);
        return kernel::make_error_code(kernel::KernelError::Success);
      }, nullptr);
    }, [](const char *) {});
    REQUIRE(ready == (failure != 2));
    REQUIRE(defaults.snapshot().bitmapBackend == ready);
  }
}
