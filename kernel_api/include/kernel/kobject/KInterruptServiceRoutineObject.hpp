#pragma once

#include <kernel/__core.hpp>
#include <kernel/kobject/KObjectPtr.hpp>
#include <kernel/kobject/KObjectT.hpp>
#include <kernel/kobject/ObjectType.hpp>

namespace kernel {

// Our KINTERRUPT equivalent, extending the root kobject_t
struct KInterruptServiceRoutineObject : KObjectT<KInterruptServiceRoutineObject, 1, TYPE_INTERRUPT> {
  cfunc<void(const KObjectPtr<KInterruptServiceRoutineObject>& interrupt_object, void *context)> service_routine;
  void *service_context;
  uint32_t vector;
  uint32_t irql;// Interrupt Request Level for priority management
};

} // namespace kernel
