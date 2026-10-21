#pragma once

#include <kernel/__core.hpp>
#include <kernel/kerror.hpp>
#include <kernel/memory/address.hpp>

#include <expected.hpp>
#include <string_view.hpp>
#include <utility.hpp>
#include <vector.hpp>

namespace kernel {

/**
 * @brief The exported symbols of one module, stored compactly.
 *
 * Names are packed back to back (NUL-terminated) in one blob and the entries
 * are {name offset, address} pairs kept sorted by name, so a lookup is a
 * binary search with exact name comparison. Call Add() for every symbol, then
 * Seal() once; Find() is only valid on a sealed table.
 */
class ExportTable {
 public:
  ExportTable() = default;
  ExportTable(const ExportTable &) = delete;
  ExportTable &operator=(const ExportTable &) = delete;
  ExportTable(ExportTable &&) noexcept = default;
  ExportTable &operator=(ExportTable &&) noexcept = default;

  void Reserve(size_t symbols, size_t nameBytes) {
    _entries.reserve(symbols);
    _names.reserve(nameBytes);
  }

  kstd::error_code Add(const kstd::string_view &name, VirtualAddress address) {
    if (name.empty()) return KernelError::InvalidArgument;
    const size_t offset = _names.size();
    _names.reserve(offset + name.size() + 1);
    for (const char c: name) _names.push_back(c);
    _names.push_back('\0');
    _entries.push_back(Entry{static_cast<uint32_t>(offset), address});
    _sealed = false;
    return KernelError::Success;
  }

  // Sorts the entries; fails with AlreadyExists when two symbols share a name.
  kstd::error_code Seal() {
    const size_t count = _entries.size();
    for (size_t gap = count / 2; gap > 0; gap /= 2) {
      for (size_t i = gap; i < count; ++i) {
        Entry moving = _entries[i];
        size_t j = i;
        for (; j >= gap && NameOf(_entries[j - gap]) > NameOf(moving); j -= gap) _entries[j] = _entries[j - gap];
        _entries[j] = moving;
      }
    }
    for (size_t i = 1; i < count; ++i) {
      if (NameOf(_entries[i - 1]) == NameOf(_entries[i])) return KernelError::AlreadyExists;
    }
    _sealed = true;
    return KernelError::Success;
  }

  [[nodiscard]] kstd::expected<VirtualAddress, kstd::error_code> Find(const kstd::string_view &name) const {
    size_t low = 0;
    size_t high = _entries.size();
    while (low < high) {
      const size_t mid = low + (high - low) / 2;
      const auto candidate = NameOf(_entries[mid]);
      if (candidate == name) return _entries[mid].address;
      if (candidate < name) low = mid + 1;
      else high = mid;
    }
    return kstd::unexpected(KernelError::NotFound);
  }

  [[nodiscard]] size_t size() const noexcept { return _entries.size(); }
  [[nodiscard]] size_t name_bytes() const noexcept { return _names.size(); }
  [[nodiscard]] bool sealed() const noexcept { return _sealed; }
  [[nodiscard]] kstd::string_view NameAt(size_t index) const { return NameOf(_entries[index]); }
  [[nodiscard]] VirtualAddress AddressAt(size_t index) const { return _entries[index].address; }

 private:
  struct Entry {
    uint32_t name_offset;
    VirtualAddress address;
  };

  [[nodiscard]] kstd::string_view NameOf(const Entry &entry) const { return kstd::string_view(_names.data() + entry.name_offset); }

  kstd::vector<char> _names;
  kstd::vector<Entry> _entries;
  bool _sealed{false};
};

}// namespace kernel
