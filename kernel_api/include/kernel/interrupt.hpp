
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

// Portably connects a hardware interrupt vector to a registered interrupt object
UNDOS_HAL_API bool connect_interrupt(kernel::kinterrupt_t *interrupt, uint32_t vector, kernel::interrupt_handler_t handler, void *context) noexcept;
UNDOS_HAL_API void disconnect_interrupt(kernel::kinterrupt_t *interrupt) noexcept;
