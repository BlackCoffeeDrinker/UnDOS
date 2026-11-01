#pragma once

#include "gfx/font/face.hpp"
#include "ob_test_support.hpp"
#include <catch2/catch_test_macros.hpp>
#include <vector.hpp>

inline auto publish_font_resource(kernel::font::FaceRegistry &registry,
                                  kstd::vector<uint8_t> bytes, uint32_t &validation) {
  EnsureObjectManagerInitialized();
  auto storage = kernel::font::FontResourceStorage::allocate(bytes.size());
  REQUIRE(storage);
  for (size_t i = 0; i < bytes.size(); ++i) storage.data()[i] = bytes[i];
  return registry.publish(static_cast<kernel::font::FontResourceStorage &&>(storage), validation);
}
