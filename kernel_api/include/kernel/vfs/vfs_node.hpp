
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
};
}// namespace kernel
