#pragma once

#include "../../../Apps/Fontloader/decoder.hpp"
#include <vector.hpp>

// The decoder is userland code; kernel APIs under test take kstd containers.
inline kstd::vector<uint8_t> decode_to_kernel(undos::font::Bytes file,
                                              undos::font::loader::DecodeError &error) {
  undos::font::loader::GrowableArray<uint8_t> output;
  error = undos::font::loader::decode(file, output);
  kstd::vector<uint8_t> bytes;
  bytes.reserve(output.size());
  for (const auto byte : output) bytes.push_back(byte);
  return bytes;
}

inline undos::font::loader::DecodeError decode_into(undos::font::Bytes file,
                                                    kstd::vector<uint8_t> &resource) {
  undos::font::loader::DecodeError error{};
  resource = decode_to_kernel(file, error);
  return error;
}
