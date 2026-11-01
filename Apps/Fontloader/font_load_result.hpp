#pragma once

#include <undos/syscalls.h>

namespace fontloader {
enum class LoadResult : uint32_t {
  Success = 0,
  Fatal = 1,
  ConfiguredFontNotFound = 2,
  UploadOutOfMemory = 3,
};

inline LoadResult open_result(uint32_t status) {
  if (status == SyscallReturnCode_OKAY) return LoadResult::Success;
  return status == SyscallReturnCode_NOT_FOUND ? LoadResult::ConfiguredFontNotFound : LoadResult::Fatal;
}

inline LoadResult upload_result(uint32_t status) {
  if (status == SyscallReturnCode_OKAY) return LoadResult::Success;
  return status == SyscallReturnCode_OUT_OF_MEMORY ? LoadResult::UploadOutOfMemory : LoadResult::Fatal;
}

inline bool fallback_eligible(uint32_t exitCode) {
  return exitCode == static_cast<uint32_t>(LoadResult::ConfiguredFontNotFound) ||
         exitCode == static_cast<uint32_t>(LoadResult::UploadOutOfMemory);
}

// A failed commit may have consumed its upload capability. Only that close
// accepts INVALID_ARGUMENT as an already-released staging handle.
template<typename Begin, typename Append, typename Commit, typename Close, typename Log>
LoadResult upload_resource(const uint8_t *data, uint32_t size, uint32_t &face,
                           Begin begin, Append append, Commit commit, Close close, Log log) {
  uint32_t upload = 0;
  auto status = begin(size, upload);
  if (status != SyscallReturnCode_OKAY) {
    log("fontloader: begin upload failed", status);
    return upload_result(status);
  }
  for (uint32_t offset = 0; offset < size;) {
    const uint32_t remaining = size - offset;
    const uint32_t count = remaining > 4096 ? 4096 : remaining;
    status = append(upload, offset, data + offset, count);
    if (status != SyscallReturnCode_OKAY) {
      log("fontloader: append upload failed", status);
      const auto closed = close(upload);
      if (closed != SyscallReturnCode_OKAY) {
        log("fontloader: staging close failed", closed);
        return LoadResult::Fatal;
      }
      return upload_result(status);
    }
    offset += count;
  }
  uint32_t validationError = 0;
  status = commit(upload, face, validationError);
  if (status != SyscallReturnCode_OKAY) {
    log("fontloader: commit rejected", status);
    const auto closed = close(upload);
    if (closed != SyscallReturnCode_OKAY && closed != SyscallReturnCode_INVALID_ARGUMENT) {
      log("fontloader: staging close failed", closed);
      return LoadResult::Fatal;
    }
    return upload_result(status);
  }
  return LoadResult::Success;
}

// The registered regular face must be explicitly closed before an eligible
// second-face failure can be reported to Init.
template<typename Load, typename Close>
LoadResult load_pair(const char *regularPath, const char *boldPath,
                     uint32_t &regular, uint32_t &bold, Load load, Close close) {
  const auto first = load(regularPath, regular);
  if (first != LoadResult::Success) return first;
  const auto second = load(boldPath, bold);
  if (second != LoadResult::Success && !close(regular)) return LoadResult::Fatal;
  return second;
}

// Publication is the startup boundary: logging eligibility never authorizes
// desktop startup until the builtin default was successfully published.
template<typename Publish, typename Log>
bool finish_startup(uint32_t exitCode, Publish publishBitmap, Log log) {
  if (exitCode == static_cast<uint32_t>(LoadResult::Success)) return true;
  if (!fallback_eligible(exitCode)) return false;
  log(exitCode == static_cast<uint32_t>(LoadResult::ConfiguredFontNotFound)
      ? "init: bitmap fallback: configured font file not found"
      : "init: bitmap fallback: kernel font upload out of memory");
  if (!publishBitmap()) {
    log("init: cannot publish fallback bitmap font; desktop not started");
    return false;
  }
  log("init: bitmap fallback selected");
  return true;
}
}
