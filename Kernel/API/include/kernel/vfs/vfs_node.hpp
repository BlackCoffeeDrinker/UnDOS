
#pragma once

namespace kernel {
enum class VFSNodeType {
  Unknown,
  File,
  Directory,
};

struct KVFSNode {
  DataBuffer fsPrivate;
  VFSNodeType type;
  uint64_t size;
  uint32_t attributes{0};
  // Packed FAT/DOS stamps, (date << 16) | time; 0 when unknown.
  uint32_t creationTime{0};
  uint32_t lastAccessTime{0};
  uint32_t lastWriteTime{0};
};

struct KVFSDirectoryEntry {
  char name[256]{};
  uint64_t size{0};
  uint32_t attributes{0};
  uint32_t creationTime{0};
  uint32_t lastAccessTime{0};
  uint32_t lastWriteTime{0};
};

struct KVFSVolumeInformation {
  char label[32]{};
  char fileSystem[16]{};
  uint32_t serialNumber{0};
  uint32_t maxComponentLength{0};
  bool readOnly{false};
  bool casePreserved{false};
};

enum class VFSFileOperation : uint32_t { Copy, Move, Delete, CreateDirectory };
}// namespace kernel
