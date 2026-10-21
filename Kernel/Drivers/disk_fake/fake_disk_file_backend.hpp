#pragma once

// Host-only backing store: backs a disk_fake device with a real file on the
// host filesystem, so the resulting disk image can be inspected after a test
// run (hex editor, loopback mount, etc). Only used by hosted tests.

#include "fake_disk.hpp"

#include <string_view.hpp>

namespace diskfake {

class FileBackingStore final : public IBackingStore {
public:
  // Opens (creating if necessary) the image file at `path`.
  explicit FileBackingStore(const kstd::string_view &path);
  ~FileBackingStore() override;

  FileBackingStore(const FileBackingStore &) = delete;
  FileBackingStore &operator=(const FileBackingStore &) = delete;

  [[nodiscard]] kstd::expected<size_t, kstd::error_code> Size() const override;
  kstd::error_code Resize(size_t newSize) override;
  kstd::error_code ReadAt(uint64_t offset, kstd::span<uint8_t> buffer) override;
  kstd::error_code WriteAt(uint64_t offset, kstd::span<const uint8_t> buffer) override;

  [[nodiscard]] bool IsOpen() const { return fd_ >= 0; }

private:
  int fd_ = -1;
};

}// namespace diskfake
