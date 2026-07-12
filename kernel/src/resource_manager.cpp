
#include "resource_manager.hpp"

#include <Kernel.hpp>
#include <new.hpp>

namespace {

using kernel::CmPartialResourceDescriptor;
using kernel::CmResourceList;
using kernel::IoResourceList;
using kernel::IoResourceRequirementsList;
using kernel::ResourceOption;
using kernel::ResourceShare;
using kernel::ResourceType;

// A device's grant: the resources it was reserved. The registry doubles as the
// global "already reserved" state the reservation core checks against.
struct ResourceAssignment : kernel::common::Link<ResourceAssignment> {
  kernel::KDevicePtr<kernel::KDevice> device;
  CmResourceList granted;
};

kernel::common::DoubleList<ResourceAssignment> s_assignments;

ResourceAssignment *FindAssignment(const kernel::KDevicePtr<kernel::KDevice> &device) {
  for (auto &s_assignment: s_assignments) {
    if (s_assignment.device.get() == device.get()) return &s_assignment;
  }
  return nullptr;
}

void FreeCmDescriptors(kernel::common::DoubleList<CmPartialResourceDescriptor> &list) {
  while (!list.empty()) {
    auto *d = &list.front();
    list.pop_front();
    KE_Free(d);
  }
}

void DestroyAssignment(ResourceAssignment *node) {
  early_print_fmt("Destroying assignment {}.\r\n", node->device->Name());

  FreeCmDescriptors(node->granted.descriptors);
  node->~ResourceAssignment();
  KE_Free(node);
}

bool AddDescriptor(IoResourceList *alt, ResourceType type, uint32_t start, uint32_t length, ResourceOption option, ResourceShare share) {
  if (!alt) return false;
  auto *d = static_cast<kernel::IoResourceDescriptor *>(KE_Malloc(sizeof(kernel::IoResourceDescriptor)));
  if (!d) return false;
  new (d) kernel::IoResourceDescriptor();
  d->type = type;
  d->share = share;
  d->option = option;
  d->start = start;
  d->end = start + (length ? length - 1 : 0);
  d->length = length;
  d->alignment = 1;
  alt->descriptors.push_back(d);
  return true;
}

}// namespace

namespace kernel::resource {
void init() {
  while (!s_assignments.empty()) {
    auto *node = &s_assignments.front();
    s_assignments.pop_front();
    DestroyAssignment(node);
  }

  early_print_fmt("Resource Manager initialized.\r\n");
}
}// namespace kernel::resource

// region Builders
UNDOS_KERNEL_API_DEF IoResourceRequirementsList *KE_RES_CreateRequirementsList(kernel::DeviceType interfaceType) noexcept {
  auto *list = static_cast<IoResourceRequirementsList *>(KE_Malloc(sizeof(IoResourceRequirementsList)));
  if (!list) return nullptr;
  new (list) IoResourceRequirementsList();
  list->interfaceType = interfaceType;
  return list;
}

UNDOS_KERNEL_API_DEF IoResourceList *KE_RES_AddAlternative(IoResourceRequirementsList *list) noexcept {
  if (!list) return nullptr;
  auto *alt = static_cast<IoResourceList *>(KE_Malloc(sizeof(IoResourceList)));
  if (!alt) return nullptr;
  new (alt) IoResourceList();
  list->alternatives.push_back(alt);
  return alt;
}

UNDOS_KERNEL_API_DEF bool KE_RES_AddPort(IoResourceList *alt, uint32_t start, uint32_t length, ResourceOption option, ResourceShare share) noexcept {
  return AddDescriptor(alt, ResourceType::Port, start, length, option, share);
}

UNDOS_KERNEL_API_DEF bool KE_RES_AddInterrupt(IoResourceList *alt, uint32_t vector, ResourceOption option, ResourceShare share) noexcept {
  return AddDescriptor(alt, ResourceType::Interrupt, vector, 1, option, share);
}

UNDOS_KERNEL_API_DEF bool KE_RES_AddMemory(IoResourceList *alt, uint32_t start, uint32_t length, ResourceOption option, ResourceShare share) noexcept {
  return AddDescriptor(alt, ResourceType::Memory, start, length, option, share);
}

UNDOS_KERNEL_API_DEF bool KE_RES_AddDma(IoResourceList *alt, uint32_t channel, ResourceOption option, ResourceShare share) noexcept {
  return AddDescriptor(alt, ResourceType::Dma, channel, 1, option, share);
}

UNDOS_KERNEL_API_DEF void KE_RES_FreeRequirementsList(IoResourceRequirementsList *list) noexcept {
  if (!list) return;
  while (!list->alternatives.empty()) {
    auto *alt = &list->alternatives.front();
    list->alternatives.pop_front();
    while (!alt->descriptors.empty()) {
      auto *d = &alt->descriptors.front();
      alt->descriptors.pop_front();
      KE_Free(d);
    }
    alt->descriptors.~DoubleList();
    KE_Free(alt);
  }
  list->alternatives.~DoubleList();
  KE_Free(list);
}

// endregion

// region Assignment / lookup
UNDOS_KERNEL_API_DEF bool KE_RES_AssignResources(const kernel::KDevicePtr<kernel::KDevice> &device, IoResourceRequirementsList *requirements) noexcept {
  if (!device) return false;

  // Re-assignment: drop any previous grant so it does not conflict with itself
  // and is not double-counted in the reserved set.
  KE_RES_ReleaseResources(device);

  auto *assignment = static_cast<ResourceAssignment *>(KE_Malloc(sizeof(ResourceAssignment)));
  if (!assignment) return false;
  new (assignment) ResourceAssignment();
  assignment->device = device;

  // No requirements -> success with an empty grant.
  if (!requirements || requirements->alternatives.empty()) {
    s_assignments.push_back(assignment);
    return true;
  }

  // Flatten every existing grant into a temporary reserved set (copies, so the
  // originals keep their list linkage).
  kernel::common::DoubleList<CmPartialResourceDescriptor> reserved;
  bool oom = false;
  for (auto it = s_assignments.begin(); it != s_assignments.end() && !oom; ++it) {
    for (const auto &descriptor: it->granted.descriptors) {
      auto *copy = static_cast<CmPartialResourceDescriptor *>(KE_Malloc(sizeof(CmPartialResourceDescriptor)));
      if (!copy) {
        oom = true;
        break;
      }
      new (copy) CmPartialResourceDescriptor();
      copy->type = descriptor.type;
      copy->share = descriptor.share;
      copy->flags = descriptor.flags;
      copy->start = descriptor.start;
      copy->length = descriptor.length;
      reserved.push_back(copy);
    }
  }

  bool ok = false;
  if (!oom) {
    // Fixed reservation only: use the first alternative.
    const IoResourceList &alt = requirements->alternatives.front();
    ok = kernel::resource::TryReserveAlternative(
        alt, reserved, assignment->granted,
        []() -> CmPartialResourceDescriptor * {
          auto *n = static_cast<CmPartialResourceDescriptor *>(KE_Malloc(sizeof(CmPartialResourceDescriptor)));
          if (n) new (n) CmPartialResourceDescriptor();
          return n;
        });
  }

  FreeCmDescriptors(reserved);

  if (!ok) {
    DestroyAssignment(assignment);
    return false;
  }

  s_assignments.push_back(assignment);
  return true;
}

UNDOS_KERNEL_API_DEF const CmResourceList *KE_RES_GetAssignedResources(const kernel::KDevicePtr<kernel::KDevice> &device) noexcept {
  const auto *a = FindAssignment(device);
  return a ? &a->granted : nullptr;
}

UNDOS_KERNEL_API_DEF const CmPartialResourceDescriptor *KE_RES_FindAssigned(const kernel::KDevicePtr<kernel::KDevice> &device, ResourceType type, size_t index) noexcept {
  auto *a = FindAssignment(device);
  if (!a) return nullptr;
  size_t seen = 0;
  for (auto &descriptor: a->granted.descriptors) {
    if (descriptor.type != type) continue;
    if (seen == index) return &descriptor;
    ++seen;
  }
  return nullptr;
}

UNDOS_KERNEL_API_DEF void KE_RES_ReleaseResources(const kernel::KDevicePtr<kernel::KDevice> &device) noexcept {
  if (!device) return;
  auto *node = FindAssignment(device);
  if (!node) return;
  s_assignments.remove(node);
  DestroyAssignment(node);
}

// endregion
