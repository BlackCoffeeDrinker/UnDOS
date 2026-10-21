#include "subsystem_loader.hpp"

#include "driver_loader.hpp"
#include "elf_loader.hpp"
#include "module/module_registry.hpp"
#include "stdkrn.hpp"
#include "syscalls/syscalls.hpp"

#include <Kernel.hpp>
#include <kernel/configuration_manager.hpp>
#include <kernel/object_manager.hpp>
#include <string.hpp>
#include <utility.hpp>
#include <vector.hpp>

namespace {
constexpr unsigned kMaxDependencyDepth = 8;

kstd::expected<uintptr_t, kstd::error_code> ResolveForModule(kstd::string_view name) {
  return kernel::module::Resolve(name);
}

bool IsLoaded(const kstd::string_view &name) { return static_cast<bool>(kernel::module::Find(name)); }

kstd::string ReadString(const kernel::KConfigurationKeyPtr &key) {
  kstd::string value;
  const auto size = KE_CM_GetKeyValueStringSize(key);
  if (!size) return value;
  value.resize(*size);
  if (!KE_CM_GetKeyValueString(key, kstd::span(value.data(), value.size()))) value.clear();
  return value;
}

kernel::KConfigurationKeyPtr OpenSubsystemKey(const kstd::string_view &name) {
  const auto root = KE_CM_OpenRoot("LocalMachine");
  if (!root) return nullptr;
  const auto key = KE_CM_OpenKey(*root, "Subsystems");
  if (!key) return nullptr;
  const auto child = KE_CM_OpenKey(*key, name);
  return child ? *child : nullptr;
}

kstd::expected<kernel::KObjectPtr<kernel::KSubsystemObject>, kstd::error_code> LoadImage(const kstd::string_view &path, unsigned depth);

// Everything a module image declares about itself: the name and dependencies
// come from its ".note.undos.module" note, the exports from its symbol table.
struct ImageInfo {
  kstd::string name;
  kstd::vector<kstd::string> dependencies;
  kernel::ExportTable exports;
  bool hasNote{false};
};

constexpr uint32_t kModuleNoteType = 1;
constexpr kstd::string_view kModuleNoteOwner = "UnDOS";
constexpr kstd::string_view kModuleNoteSection = ".note.undos.module";

struct NoteHeader {
  uint32_t namesz;
  uint32_t descsz;
  uint32_t type;
};

constexpr size_t AlignNote(size_t value) { return (value + 3) & ~size_t(3); }

// The note's descriptor is "<name>\0<dependency>\0...\0": the first string is
// the module name, every following non-empty string a required module.
void CaptureModuleNote(void *context, kstd::string_view sectionName, const uint8_t *data, size_t size) {
  if (sectionName != kModuleNoteSection || size < sizeof(NoteHeader)) return;
  auto &info = *static_cast<ImageInfo *>(context);

  NoteHeader header{};
  kstd::memcpy(&header, data, sizeof(header));
  const size_t descOffset = sizeof(header) + AlignNote(header.namesz);
  if (header.type != kModuleNoteType || header.namesz != kModuleNoteOwner.size() + 1 ||
      kstd::string_view(reinterpret_cast<const char *>(data + sizeof(header))) != kModuleNoteOwner ||
      descOffset > size || header.descsz > size - descOffset) return;

  const char *cursor = reinterpret_cast<const char *>(data + descOffset);
  const char *end = cursor + header.descsz;
  bool first = true;
  while (cursor < end && *cursor) {
    const kstd::string_view item(cursor);
    if (first) info.name = kstd::string(item);
    else info.dependencies.push_back(kstd::string(item));
    first = false;
    cursor += item.size() + 1;
  }
  info.hasNote = !info.name.empty();
}

void CaptureModuleExport(void *context, kstd::string_view name, kernel::VirtualAddress address) {
  static_cast<ImageInfo *>(context)->exports.Add(name, address);
}

kstd::error_code EnsureRequired(const ImageInfo &info, unsigned depth) {
  for (const auto &dependency: info.dependencies) {
    const kstd::string_view required(dependency);
    if (IsLoaded(required)) continue;
    if (depth >= kMaxDependencyDepth) return kernel::KernelError::InvalidArgument;

    const auto key = OpenSubsystemKey(required);
    if (!key) return kernel::KernelError::NotFound;
    const auto imagePathKey = KE_CM_OpenKey(key, "ImagePath");
    if (!imagePathKey) return kernel::KernelError::NotFound;
    const auto imagePath = ReadString(*imagePathKey);
    if (imagePath.empty()) return kernel::KernelError::NotFound;

    const auto loaded = LoadImage(imagePath, depth + 1);
    if (!loaded) return loaded.error();
  }
  return {};
}

kstd::expected<kernel::KObjectPtr<kernel::KSubsystemObject>, kstd::error_code> LoadImage(const kstd::string_view &path, unsigned depth) {
  if (path.empty()) return kstd::unexpected(kernel::KernelError::InvalidArgument);

  ImageInfo info;
  kernel::elf::LoadPolicy policy;
  policy.relocatable = true;
  policy.context = &info;
  policy.resolve_symbol = kernel::cfunc<kstd::expected<uintptr_t, kstd::error_code>(kstd::string_view)>(ResolveForModule);
  policy.capture_section = kernel::cfunc<void(void *, kstd::string_view, const uint8_t *, size_t)>(CaptureModuleNote);
  policy.capture_section_name = kModuleNoteSection;
  policy.capture_export = kernel::cfunc<void(void *, kstd::string_view, kernel::VirtualAddress)>(CaptureModuleExport);

  const auto image = kernel::elf::LoadElfFromVfs(path, policy);
  if (!image) {
    KE_Print("KE_SUBSYSTEM_Load: failed to load '{}': {}", path, image.error().message());
    return kstd::unexpected(image.error());
  }

  if (!info.hasNote) {
    KE_Print("KE_SUBSYSTEM_Load: '{}' has no module note", path);
    return kstd::unexpected(kernel::KernelError::InvalidExecutable);
  }

  const kstd::string_view moduleName(info.name);
  if (IsLoaded(moduleName)) return kstd::unexpected(kernel::KernelError::AlreadyExists);

  if (const auto status = info.exports.Seal()) {
    KE_Print("KE_SUBSYSTEM_Load: '{}' exports duplicate symbols", moduleName);
    return kstd::unexpected(status);
  }

  if (const auto status = EnsureRequired(info, depth)) {
    KE_Print("KE_SUBSYSTEM_Load: '{}' has an unavailable dependency: {}", moduleName, status.message());
    return kstd::unexpected(status);
  }

  auto module = kernel::CreateKObject<kernel::KModuleObject>(kstd::static_string<64>(moduleName));
  if (!module) return kstd::unexpected(module.error());
  (*module)->load_base = image->load_base;
  (*module)->total_size = image->total_size;
  (*module)->dependencies = kstd::move(info.dependencies);
  (*module)->exports = kstd::move(info.exports);
  (*module)->entry_point = image->entry_point.as_ptr<void(const kernel::KObjectPtr<kernel::KModuleObject> &)>();

  // Modules stay resident for now, so the registry holds the only owner.
  if (const auto status = kernel::module::Register(*module)) return kstd::unexpected(status);

  if (const auto status = kernel::syscall::BindProvider(*module)) {
    KE_Print("KE_SUBSYSTEM_Load: '{}' could not bind its syscalls: {}", moduleName, status.message());
    return kstd::unexpected(status);
  }

  auto subsystem = kernel::CreateKObject<kernel::KSubsystemObject>(kstd::static_string<64>(moduleName));
  if (!subsystem) return kstd::unexpected(subsystem.error());
  (*subsystem)->module = *module;

  if (const auto status = KE_OB_InsertObject(KE_OB_GetSubsystemDirectory(), *subsystem)) return kstd::unexpected(status);

  (*module)->entry_point(*module);
  KE_Print("Subsystem '{}' loaded ({} exports)", moduleName, (*module)->exports.size());
  return *subsystem;
}
}// namespace

namespace kernel::subsystem {
kstd::expected<KObjectPtr<KSubsystemObject>, kstd::error_code> Load(const kstd::string_view &path) {
  return LoadImage(path, 0);
}

void LoadAutomaticStartSubsystems() {
  const auto root = KE_CM_OpenRoot("LocalMachine");
  if (!root) return;
  const auto subsystems = KE_CM_OpenKey(*root, "Subsystems");
  if (!subsystems) return;

  for (uint32_t index = 0;; ++index) {
    const auto child = KE_CM_EnumKey(*subsystems, index);
    if (!child || !child->available) break;
    const kstd::string childName(child->name);
    if (IsLoaded(childName)) continue;

    const auto key = KE_CM_OpenKey(*subsystems, childName);
    if (!key) continue;
    const auto startKey = KE_CM_OpenKey(*key, "Start");
    const auto imageKey = KE_CM_OpenKey(*key, "ImagePath");
    if (!startKey || !imageKey || ReadString(*startKey) != "Automatic") continue;

    const auto imagePath = ReadString(*imageKey);
    KE_Print("Loading automatic-start subsystem '{}' from '{}'", childName, imagePath);
    if (const auto loaded = LoadImage(imagePath, 0); !loaded) {
      KE_Print("Subsystem '{}' failed: {}", childName, loaded.error().message());
    }
  }
}
}// namespace kernel::subsystem
