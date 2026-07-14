#pragma once

#include <string_view.hpp>

// Converts `name` into an 8.3 short (FAT) directory-entry name, writing the
// 11 upper-cased, space-padded characters into `out`.
//
// `out` must point at a buffer of at least 11 characters; its length is not
// otherwise used. Returns false (and leaves `out` in a partially-written
// state) if the base name is longer than 8 characters or the extension is
// longer than 3 characters.
inline bool FAT_ToShortName(const kstd::string_view &name, kstd::string_view out) {
  for (size_t i = 0; i < 11; i++) out[i] = ' ';

  const auto dot = name.rfind('.');
  const auto base = (dot == kstd::string_view::npos) ? name : name.substr(0, dot);
  const auto ext = (dot == kstd::string_view::npos) ? kstd::string_view{} : name.substr(dot + 1);

  if (base.size() > 8 || ext.size() > 3) return false;

  for (size_t i = 0; i < base.size(); i++) {
    char c = base[i];
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    out[i] = c;
  }
  for (size_t i = 0; i < ext.size(); i++) {
    char c = ext[i];
    if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    out[8 + i] = c;
  }

  return true;
}
