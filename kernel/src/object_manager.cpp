
#include "vmm.hpp"
#include <Kernel.hpp>
#include <memory.hpp>

namespace {
class RootNode : public kernel::KObject {
  public:
  RootNode() : kernel::KObject(kernel::ObjectType::from_literal("ROOT")) {
  }
};

RootNode *root;
}// namespace


namespace kernel::object_manager {
void init() {
  root = nullptr;
}
}// namespace kernel::object_manager
