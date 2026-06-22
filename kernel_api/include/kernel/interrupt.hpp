
#pragma once

#include <kernel/__core.hpp>
#include <kernel/object.hpp>

namespace kernel {
using interrupt_handler_t = void (*)(kobject_t *interrupt_object, void *context);

// Our KINTERRUPT equivalent, extending the root kobject_t
struct kinterrupt_t : public kobject_t {
  interrupt_handler_t service_routine;
  void *service_context;
  uint32_t vector;
  uint32_t irql;// Interrupt Request Level for priority management
};
}// namespace kernel
