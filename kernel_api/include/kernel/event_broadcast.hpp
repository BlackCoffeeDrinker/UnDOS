
#pragma once

#include <kernel/data_buffer.hpp>
#include <kernel/fwd/KDevicePtr.hpp>
#include <kernel/fwd/KObjectPtr.hpp>
#include <kernel/kerror.hpp>
#include <expected.hpp>

namespace kernel::event {
/// Target event source tag and container for KObject / KDevice
struct Source {
  enum class Type {
    KObject,
    KDevice,
  };

  Type type{Type::KObject};
  KObjectPtr<KObject> kobject{};
  KDevicePtr<KDevice> kdevice{};

  Source() = default;

  explicit Source(const KObjectPtr<KObject> &obj) : type(Type::KObject), kobject(obj) {}
  explicit Source(const KDevicePtr<KDevice> &dev) : type(Type::KDevice), kdevice(dev) {}

  ~Source() = default;
  Source(const Source &) = default;
  Source &operator=(const Source &) = default;
  Source(Source &&) noexcept = default;
  Source &operator=(Source &&) noexcept = default;

  bool operator==(const Source &other) const noexcept {
    if (type != other.type) { return false; }
    if (type == Type::KObject) { return kobject.get() == other.kobject.get(); }
    return kdevice.get() == other.kdevice.get();
  }

  template<typename T>
    requires(kstd::is_base_of_v<KObject, T>)
  KObjectPtr<T> As() const {
    if (type == Type::KObject) {
      return kobject.As<T>();
    }
    return nullptr;
  }

  template<typename T>
    requires(kstd::is_base_of_v<KDevice, T>)
  KDevicePtr<T> As() const {
    if (type == Type::KDevice) {
      return kdevice.As<T>();
    }
    return nullptr;
  }
};
class Category;
class Code;

template<typename T>
struct is_code_enum : kstd::false_type {};

template<typename T>
inline constexpr bool is_code_enum_v = is_code_enum<T>::value;

[[nodiscard]] const Category &system_category() noexcept;

class Category {
  public:
  constexpr Category() noexcept = default;
  Category(const Category &) = delete;
  Category &operator=(const Category &) = delete;
  [[nodiscard]] virtual const char *name() const noexcept = 0;
  [[nodiscard]] virtual kstd::string_view message(int /*ev*/) const noexcept = 0;
  [[nodiscard]] virtual bool equivalent(const Code &code) const noexcept = 0;
  [[nodiscard]] bool operator==(const Category &rhs) const noexcept { return this == &rhs; }

  protected:
  // Non-virtual and trivial so function-local static categories need no
  // atexit-registered destructor (there is no atexit in the kernel).
  ~Category() = default;
};

class Code {
  int val_;
  const Category *cat_;

  public:
  Code() noexcept : val_(0), cat_(&system_category()) {}
  constexpr Code(int val, const Category &cat) noexcept : val_(val), cat_(&cat) {}

  [[nodiscard]] constexpr int value() const noexcept { return val_; }
  [[nodiscard]] constexpr const Category &category() const noexcept { return *cat_; }
  [[nodiscard]] kstd::string_view message() const noexcept { return cat_ ? cat_->message(val_) : ""; }

  void clear() noexcept {
    val_ = 0;
    cat_ = &system_category();
  }

  template<typename Enum>
    requires is_code_enum_v<Enum>
  bool is(const Category &cat, Enum val) const noexcept { return cat_ == &cat && val_ == static_cast<int>(val); }

  [[nodiscard]] bool operator==(const Code &rhs) const { return cat_ == rhs.cat_ && val_ == rhs.val_; }
  explicit constexpr operator bool() const noexcept { return val_ != 0; }
};

template<typename Enum>
  requires is_code_enum_v<Enum>
[[nodiscard]] constexpr Code make_event_code(const Category &cat, Enum e) noexcept {
  return Code(static_cast<int>(e), cat);
}

using EventCallbackFn = cfunc<void, const Source &, const Code &, const DataBuffer &>;

struct EventInterestToken {
  uint64_t id{0};

  EventInterestToken() = default;
  explicit EventInterestToken(uint64_t tokenVal) : id(tokenVal) {}
  ~EventInterestToken();

  EventInterestToken(const EventInterestToken &) = delete;
  EventInterestToken &operator=(const EventInterestToken &) = delete;

  EventInterestToken(EventInterestToken &&other) noexcept : id(other.id) {
    other.id = 0;
  }

  EventInterestToken &operator=(EventInterestToken &&other) noexcept;

  explicit operator bool() const { return id != 0; }
};

struct RegistrationContext {
  EventCallbackFn fn;
  DataBuffer user_buffer;
};
}// namespace kernel::event

namespace kernel {
using EventInterestResult = kstd::expected<event::EventInterestToken, KernelError>;
}// namespace kernel

/**
 * @ingroup Events
 * @brief Registers interest in events raised against @p target.
 *
 * Thread-context only: this takes a yielding mutex and must not be called from
 * an ISR. Callbacks are invoked on the kernel event pump thread with no
 * broadcaster lock held, so a callback may freely register, unregister or
 * broadcast.
 *
 * @return A move-only token. Destroying it unregisters the interest.
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::EventInterestResult,
                          KE_EVENT_RegisterInterest,
                          const kernel::event::Source &target,
                          kernel::event::RegistrationContext &&context);

/**
 * @ingroup Events
 * @brief Unregisters interest in an event.
 *
 * Thread-context only, and **may block**: if a callback for this token is
 * currently running on the event pump thread, this waits for it to finish, so
 * once it returns the caller is guaranteed that no callback for the token is
 * running or can start.
 *
 * The one exception is a callback unregistering its own token (directly or by
 * letting the token go out of scope): waiting there would deadlock, so the
 * subscriber is instead torn down as soon as the callback returns.
 */
UNDOS_KERNEL_PUBLIC_V1API(kernel::KernelError,
                          KE_EVENT_UnregisterInterest,
                          kernel::event::EventInterestToken &);

namespace kernel::event {
inline EventInterestToken::~EventInterestToken() {
  if (id != 0) (void) KE_EVENT_UnregisterInterest(*this);
}

inline EventInterestToken &EventInterestToken::operator=(EventInterestToken &&other) noexcept {
  if (this != &other) {
    if (id != 0) (void) KE_EVENT_UnregisterInterest(*this);
    id = other.id;
    other.id = 0;
  }
  return *this;
}

}// namespace kernel::event
