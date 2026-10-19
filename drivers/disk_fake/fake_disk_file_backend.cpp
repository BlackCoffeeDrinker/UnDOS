#include "fake_disk_file_backend.hpp"

#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace diskfake {

namespace {
// string_view isn't guaranteed to be NUL-terminated; copy into a bounded
// buffer before handing the path to POSIX APIs.
constexpr size_t kMaxPathLength = 4096;
}// namespace

FileBackingStore::FileBackingStore(const kstd::string_view &path) {
  char pathBuf[kMaxPathLength];
  const size_t len = path.size() < kMaxPathLength - 1 ? path.size() : kMaxPathLength - 1;
  std::memcpy(pathBuf, path.data(), len);
  pathBuf[len] = '\0';

  fd_ = ::open(pathBuf, O_CREAT | O_RDWR, 0644);
}

FileBackingStore::~FileBackingStore() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
}

kstd::expected<size_t, kernel::KernelError> FileBackingStore::Size() const {
  if (fd_ < 0) return kstd::unexpected(kernel::KernelError::InvalidState);
  struct stat st{};
  if (::fstat(fd_, &st) != 0) return kstd::unexpected(kernel::KernelError::DeviceError);
  return static_cast<size_t>(st.st_size);
}

kernel::KernelError FileBackingStore::Resize(size_t newSize) {
  if (fd_ < 0) return kernel::KernelError::InvalidState;
  return ::ftruncate(fd_, static_cast<off_t>(newSize)) == 0 ? kernel::KernelError::Success : kernel::KernelError::DeviceError;
}

kernel::KernelError FileBackingStore::ReadAt(uint64_t offset, kstd::span<uint8_t> buffer) {
  if (fd_ < 0) return kernel::KernelError::InvalidState;
  size_t total = 0;
  while (total < buffer.size()) {
    const ssize_t n = ::pread(fd_, buffer.data() + total, buffer.size() - total, static_cast<off_t>(offset + total));
    if (n < 0) return kernel::KernelError::DeviceError;
    if (n == 0) break;
    total += static_cast<size_t>(n);
  }
  return total == buffer.size() ? kernel::KernelError::Success : kernel::KernelError::UnexpectedEndOfFile;
}

kernel::KernelError FileBackingStore::WriteAt(uint64_t offset, kstd::span<const uint8_t> buffer) {
  if (fd_ < 0) return kernel::KernelError::InvalidState;
  size_t total = 0;
  while (total < buffer.size()) {
    const ssize_t n = ::pwrite(fd_, buffer.data() + total, buffer.size() - total, static_cast<off_t>(offset + total));
    if (n <= 0) return kernel::KernelError::DeviceError;
    total += static_cast<size_t>(n);
  }
  return kernel::KernelError::Success;
}

}// namespace diskfake
