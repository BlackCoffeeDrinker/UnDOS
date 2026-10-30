
#include <kernel/kerror.hpp>
#include <new.hpp>

namespace {
class KernelErrorCategory : public kstd::error_category {
  public:
  [[nodiscard]] const char *name() const noexcept override { return "kernel_error_category"; }
  [[nodiscard]] kstd::string_view message(int __ev) const noexcept override {
    switch (static_cast<kernel::KernelError>(__ev)) {
      case kernel::KernelError::Success: return "Success";
      case kernel::KernelError::Unknown: return "Unknown error";
      case kernel::KernelError::InvalidArgument: return "Invalid argument";
      case kernel::KernelError::InvalidSize: return "Invalid size";
      case kernel::KernelError::InvalidAlignment: return "Invalid alignment";
      case kernel::KernelError::InvalidOffset: return "Invalid offset";
      case kernel::KernelError::OutOfRange: return "Out of range";
      case kernel::KernelError::ArithmeticOverflow: return "Arithmetic overflow";
      case kernel::KernelError::NameTooLong: return "Name too long";
      case kernel::KernelError::BufferTooSmall: return "Buffer too small";
      case kernel::KernelError::InvalidFormat: return "Invalid format";
      case kernel::KernelError::TooManyArguments: return "Too many arguments";
      case kernel::KernelError::InvalidAddress: return "Invalid address";
      case kernel::KernelError::OutOfMemory: return "Out of memory";
      case kernel::KernelError::AddressSpaceExhausted: return "Address space exhausted";
      case kernel::KernelError::ResourceLimitExceeded: return "Resource limit exceeded";
      case kernel::KernelError::QueueFull: return "Queue full";
      case kernel::KernelError::ResourceConflict: return "Resource conflict";
      case kernel::KernelError::InvalidHandle: return "Invalid handle";
      case kernel::KernelError::WrongObjectType: return "Wrong object type";
      case kernel::KernelError::NotFound: return "Not found";
      case kernel::KernelError::AlreadyExists: return "Already exists";
      case kernel::KernelError::AccessDenied: return "Access denied";
      case kernel::KernelError::AlreadyAttached: return "Already attached";
      case kernel::KernelError::ParentMismatch: return "Parent mismatch";
      case kernel::KernelError::InvalidState: return "Invalid state";
      case kernel::KernelError::NotInitialized: return "Not initialized";
      case kernel::KernelError::NoCurrentThread: return "No current thread";
      case kernel::KernelError::NoCurrentProcess: return "No current process";
      case kernel::KernelError::Unsupported: return "Unsupported operation";
      case kernel::KernelError::NotImplemented: return "Not implemented";
      case kernel::KernelError::DeviceNotReady: return "Device not ready";
      case kernel::KernelError::StandardStreamUnavailable: return "Standard stream unavailable";
      case kernel::KernelError::NotWaitable: return "Object is not waitable";
      case kernel::KernelError::Timeout: return "Operation timed out";
      case kernel::KernelError::WouldBlock: return "Operation would block";
      case kernel::KernelError::IoError: return "I/O error";
      case kernel::KernelError::DeviceError: return "Device error";
      case kernel::KernelError::UnexpectedEndOfFile: return "Unexpected end of file";
      case kernel::KernelError::InvalidData: return "Invalid data";
      case kernel::KernelError::InvalidResource: return "Invalid resource";
      case kernel::KernelError::NotDirectory: return "Not a directory";
      case kernel::KernelError::InvalidExecutable: return "Invalid executable";
      case kernel::KernelError::UnresolvedSymbol: return "Unresolved symbol";
      case kernel::KernelError::ProcessStartupFailed: return "Process startup failed";
      case kernel::KernelError::Retry: return "Retry with a current topology snapshot";
    }
    return "Unknown kernel error";
  }
};


}// namespace

extern "C" UNDOS_KERNEL_API_DEF kstd::error_category *KE_ERROR_GetCategory() noexcept {
  // Error codes retain this reference for the kernel's lifetime; never register
  // its virtual destructor with the application exit runtime.
  alignas(KernelErrorCategory) static unsigned char storage[sizeof(KernelErrorCategory)];
  static KernelErrorCategory &category = *new (storage) KernelErrorCategory;
  return &category;
}
