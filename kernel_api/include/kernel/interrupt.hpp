
#pragma once

#include <kernel/KObject.hpp>
#include <kernel/__core.hpp>

namespace kernel {
using interrupt_handler_t = void (*)(KObject *interrupt_object, void *context);

// Our KINTERRUPT equivalent, extending the root kobject_t
struct InterruptServiceRoutine : public KObject {
  interrupt_handler_t service_routine;
  void *service_context;
  uint32_t vector;
  uint32_t irql;// Interrupt Request Level for priority management
};
}// namespace kernel
