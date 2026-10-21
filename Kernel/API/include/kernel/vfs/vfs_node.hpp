
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
};

struct KVFSDirectoryEntry {
  char name[256]{};
  uint64_t size{0};
  uint32_t attributes{0};
};

enum class VFSFileOperation : uint32_t { Copy, Move, Delete, CreateDirectory };
}// namespace kernel
