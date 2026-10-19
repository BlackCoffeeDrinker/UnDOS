#include <catch2/catch_test_macros.hpp>

#include <kernel/kobject/KDirectoryObject.hpp>
#include <kernel/object_manager.hpp>

#include "ob_test_support.hpp"

// The broadcaster under test. Tests drive delivery synchronously through
// kernel::event::detail::pump_once() instead of standing up a real pump
// thread, so kernel::event::init() is never called here.
#include "event_broadcaster.hpp"

namespace {

// Category for test events; events carry a (value, category) Code.
const kernel::event::Category &TestCategory() {
  struct Category final : kernel::event::Category {
    [[nodiscard]] const char *name() const noexcept override { return "test"; }
    [[nodiscard]] kstd::string_view message(int) const noexcept override { return ""; }
    [[nodiscard]] bool equivalent(const kernel::event::Code &code) const noexcept override { return &code.category() == this; }
  };
  static Category category;
  return category;
}

kernel::event::Code Ev(uint32_t value) { return kernel::event::Code(static_cast<int>(value), TestCategory()); }

kernel::event::EventInterestToken RegisterInterest(const kernel::event::Source &source,
                                                  kernel::event::RegistrationContext &&context) {
  auto token = KE_EVENT_RegisterInterest(source, kstd::move(context));
  REQUIRE(token);
  return kstd::move(*token);
}

// Recorded callback invocations, shared by the free-function callbacks below
// (EventCallbackFn is a plain function pointer, so there are no captures).
struct CallLog {
  uint32_t events[16]{};
  int count{0};
  bool foreign{false};
  void Record(const kernel::event::Code &code) {
    if (count < 16) events[count] = static_cast<uint32_t>(code.value());
    if (&code.category() != &TestCategory()) foreign = true;
    ++count;
  }
};

CallLog g_logA;
CallLog g_logB;
CallLog g_logC;

void CallbackA(const kernel::event::Source &, const kernel::event::Code &code, const kernel::DataBuffer &) { g_logA.Record(code); }
void CallbackB(const kernel::event::Source &, const kernel::event::Code &code, const kernel::DataBuffer &) { g_logB.Record(code); }
void CallbackC(const kernel::event::Source &, const kernel::event::Code &code, const kernel::DataBuffer &) { g_logC.Record(code); }

// Token belonging to a subscriber that a callback unregisters.
kernel::event::EventInterestToken g_tokenToUnregister;

void CallbackUnregistersOther(const kernel::event::Source &, const kernel::event::Code &code, const kernel::DataBuffer &) {
  g_logA.Record(code);
  KE_EVENT_UnregisterInterest(g_tokenToUnregister);
}

// Token belonging to the subscriber whose own callback this is.
kernel::event::EventInterestToken g_selfToken;

void CallbackUnregistersSelf(const kernel::event::Source &, const kernel::event::Code &code, const kernel::DataBuffer &) {
  g_logA.Record(code);
  KE_EVENT_UnregisterInterest(g_selfToken);
}

kernel::event::EventInterestToken g_tokenFromCallback;
kernel::event::Source g_sourceForNestedRegister;

void CallbackRegistersAnother(const kernel::event::Source &, const kernel::event::Code &code, const kernel::DataBuffer &) {
  g_logA.Record(code);
  g_tokenFromCallback = RegisterInterest(
      g_sourceForNestedRegister,
      kernel::event::RegistrationContext{.fn = &CallbackB, .user_buffer = {}});
}

// Any KObject works as an event source; a directory is the cheapest one the
// hosted harness can create.
kernel::KObjectPtr<kernel::KObject> MakeSource(const char *name) {
  auto dir = kernel::CreateKObject<kernel::KDirectoryObject>(name);
  REQUIRE(dir);
  return *dir;
}

struct Fixture {
  Fixture() {
    EnsureObjectManagerInitialized();
    kernel::event::detail::reset();
    g_logA = CallLog{};
    g_logB = CallLog{};
    g_logC = CallLog{};
  }
  ~Fixture() { kernel::event::detail::reset(); }
};

// Drains every queued broadcast.
int PumpAll() {
  int dispatched = 0;
  while (kernel::event::detail::pump_once()) ++dispatched;
  return dispatched;
}

}// namespace

TEST_CASE("Broadcast reaches every subscriber of the same source", "[event]") {
  Fixture fixture;
  const auto source = MakeSource("evt-multi");

  // Regression: broadcast() used to re-lock the (non-recursive) queue mutex
  // once per matching subscriber, so a second subscriber on the same source
  // deadlocked the broadcasting thread forever.
  auto tokenA = RegisterInterest(kernel::event::Source(source),
                                          kernel::event::RegistrationContext{.fn = &CallbackA, .user_buffer = {}});
  auto tokenB = RegisterInterest(kernel::event::Source(source),
                                          kernel::event::RegistrationContext{.fn = &CallbackB, .user_buffer = {}});
  auto tokenC = RegisterInterest(kernel::event::Source(source),
                                          kernel::event::RegistrationContext{.fn = &CallbackC, .user_buffer = {}});
  REQUIRE(tokenA);
  REQUIRE(tokenB);
  REQUIRE(tokenC);

  kernel::event::broadcast(source, Ev(7u));

  // One queued record fans out to all three subscribers on the pump side.
  REQUIRE(PumpAll() == 1);
  REQUIRE(g_logA.count == 1);
  REQUIRE(g_logB.count == 1);
  REQUIRE(g_logC.count == 1);
  REQUIRE(g_logA.events[0] == 7u);
  REQUIRE(g_logB.events[0] == 7u);
  REQUIRE(g_logC.events[0] == 7u);
  // The category travels with the code to every subscriber.
  REQUIRE_FALSE(g_logA.foreign);
  REQUIRE_FALSE(g_logB.foreign);
  REQUIRE_FALSE(g_logC.foreign);
}

TEST_CASE("Broadcast does not reach subscribers of another source", "[event]") {
  Fixture fixture;
  const auto wanted = MakeSource("evt-wanted");
  const auto other = MakeSource("evt-other");

  auto tokenA = RegisterInterest(kernel::event::Source(wanted),
                                          kernel::event::RegistrationContext{.fn = &CallbackA, .user_buffer = {}});
  auto tokenB = RegisterInterest(kernel::event::Source(other),
                                          kernel::event::RegistrationContext{.fn = &CallbackB, .user_buffer = {}});

  kernel::event::broadcast(wanted, Ev(1u));
  PumpAll();

  REQUIRE(g_logA.count == 1);
  REQUIRE(g_logB.count == 0);
}

TEST_CASE("Broadcast with no subscribers drains cleanly", "[event]") {
  Fixture fixture;
  const auto source = MakeSource("evt-none");

  kernel::event::broadcast(source, Ev(3u));
  REQUIRE(PumpAll() == 1);
  REQUIRE(g_logA.count == 0);
  REQUIRE(kernel::event::detail::dropped_count() == 0);
}

TEST_CASE("Registering with a null callback yields an empty token", "[event]") {
  Fixture fixture;
  const auto source = MakeSource("evt-null");

  const auto result = KE_EVENT_RegisterInterest(kernel::event::Source(source),
                                                kernel::event::RegistrationContext{});
  REQUIRE_FALSE(result);
  REQUIRE(result.error() == kernel::KernelError::InvalidArgument);
}

TEST_CASE("Unregister stops further callbacks", "[event]") {
  Fixture fixture;
  const auto source = MakeSource("evt-unreg");

  auto tokenA = RegisterInterest(kernel::event::Source(source),
                                          kernel::event::RegistrationContext{.fn = &CallbackA, .user_buffer = {}});

  kernel::event::broadcast(source, Ev(1u));
  PumpAll();
  REQUIRE(g_logA.count == 1);

  KE_EVENT_UnregisterInterest(tokenA);
  REQUIRE_FALSE(tokenA);

  kernel::event::broadcast(source, Ev(2u));
  PumpAll();
  REQUIRE(g_logA.count == 1);
}

TEST_CASE("Unregister from inside a callback suppresses the other subscriber", "[event]") {
  Fixture fixture;
  const auto source = MakeSource("evt-unreg-other");

  // Regression: work items used to hold a raw EventSubscriber*, so tearing a
  // subscriber down mid-dispatch left dangling pointers in the queue (made
  // worse by an erase loop that skipped every other element).
  auto first = RegisterInterest(kernel::event::Source(source),
                                         kernel::event::RegistrationContext{.fn = &CallbackUnregistersOther, .user_buffer = {}});
  g_tokenToUnregister = RegisterInterest(kernel::event::Source(source),
                                                  kernel::event::RegistrationContext{.fn = &CallbackB, .user_buffer = {}});

  kernel::event::broadcast(source, Ev(5u));
  PumpAll();

  REQUIRE(g_logA.count == 1);
  // The second subscriber was unregistered before the fan-out reached it.
  REQUIRE(g_logB.count == 0);
  REQUIRE_FALSE(g_tokenToUnregister);

  // The broadcaster is still healthy afterwards.
  kernel::event::broadcast(source, Ev(6u));
  PumpAll();
  REQUIRE(g_logA.count == 2);
}

TEST_CASE("A callback can unregister its own token without deadlocking", "[event]") {
  Fixture fixture;
  const auto source = MakeSource("evt-unreg-self");

  g_selfToken = RegisterInterest(kernel::event::Source(source),
                                          kernel::event::RegistrationContext{.fn = &CallbackUnregistersSelf, .user_buffer = {}});

  kernel::event::broadcast(source, Ev(9u));
  PumpAll();
  REQUIRE(g_logA.count == 1);
  REQUIRE_FALSE(g_selfToken);

  // The subscriber must have been reaped once its callback returned.
  kernel::event::broadcast(source, Ev(10u));
  PumpAll();
  REQUIRE(g_logA.count == 1);
}

TEST_CASE("A callback can register a new subscriber", "[event]") {
  Fixture fixture;
  const auto source = MakeSource("evt-nested-reg");
  g_sourceForNestedRegister = kernel::event::Source(source);

  auto first = RegisterInterest(kernel::event::Source(source),
                                         kernel::event::RegistrationContext{.fn = &CallbackRegistersAnother, .user_buffer = {}});

  kernel::event::broadcast(source, Ev(1u));
  PumpAll();
  REQUIRE(g_logA.count == 1);
  REQUIRE(g_tokenFromCallback);
  // The new subscriber appeared after the fan-out snapshot was taken.
  REQUIRE(g_logB.count == 0);

  kernel::event::broadcast(source, Ev(2u));
  PumpAll();
  REQUIRE(g_logB.count == 1);

  KE_EVENT_UnregisterInterest(g_tokenFromCallback);
  g_sourceForNestedRegister = kernel::event::Source{};
}

TEST_CASE("A callback may broadcast again", "[event]") {
  Fixture fixture;
  const auto source = MakeSource("evt-renter");

  auto tokenA = RegisterInterest(kernel::event::Source(source),
                                          kernel::event::RegistrationContext{.fn = &CallbackA, .user_buffer = {}});

  kernel::event::broadcast(source, Ev(1u));
  kernel::event::broadcast(source, Ev(2u));
  REQUIRE(PumpAll() == 2);
  REQUIRE(g_logA.count == 2);
  REQUIRE(g_logA.events[0] == 1u);
  REQUIRE(g_logA.events[1] == 2u);
}

TEST_CASE("Queue overflow drops events and is counted", "[event]") {
  Fixture fixture;
  const auto source = MakeSource("evt-overflow");

  auto tokenA = RegisterInterest(kernel::event::Source(source),
                                          kernel::event::RegistrationContext{.fn = &CallbackA, .user_buffer = {}});

  // Capacity is 64; push well past it without pumping.
  constexpr int kPushed = 100;
  for (int i = 0; i < kPushed; ++i) {
    kernel::event::broadcast(source, Ev(static_cast<uint32_t>(i)));
  }

  REQUIRE(kernel::event::detail::dropped_count() == static_cast<uint32_t>(kPushed - 64));
  REQUIRE(PumpAll() == 64);
  REQUIRE(g_logA.count == 64);
  // Drops are the newest records, so the oldest ones survived in order.
  REQUIRE(g_logA.events[0] == 0u);

  // The ring must still be usable after overflowing.
  g_logA = CallLog{};
  kernel::event::broadcast(source, Ev(999u));
  REQUIRE(PumpAll() == 1);
  REQUIRE(g_logA.count == 1);
  REQUIRE(g_logA.events[0] == 999u);
}

TEST_CASE("EventInterestToken destructor unregisters exactly once", "[event]") {
  Fixture fixture;
  const auto source = MakeSource("evt-token");

  {
    auto token = RegisterInterest(kernel::event::Source(source),
                                           kernel::event::RegistrationContext{.fn = &CallbackA, .user_buffer = {}});
    REQUIRE(token);

    kernel::event::broadcast(source, Ev(1u));
    PumpAll();
    REQUIRE(g_logA.count == 1);
  }

  kernel::event::broadcast(source, Ev(2u));
  PumpAll();
  REQUIRE(g_logA.count == 1);
}

TEST_CASE("Moving an EventInterestToken transfers ownership", "[event]") {
  Fixture fixture;
  const auto source = MakeSource("evt-token-move");

  kernel::event::EventInterestToken outer;
  {
    auto token = RegisterInterest(kernel::event::Source(source),
                                           kernel::event::RegistrationContext{.fn = &CallbackA, .user_buffer = {}});
    outer = kstd::move(token);
    REQUIRE_FALSE(token);
  }

  // The inner token's destructor must not have unregistered the interest.
  REQUIRE(outer);
  kernel::event::broadcast(source, Ev(1u));
  PumpAll();
  REQUIRE(g_logA.count == 1);

  KE_EVENT_UnregisterInterest(outer);
  kernel::event::broadcast(source, Ev(2u));
  PumpAll();
  REQUIRE(g_logA.count == 1);
}

TEST_CASE("The user buffer is handed back to the callback", "[event]") {
  Fixture fixture;
  const auto source = MakeSource("evt-buffer");

  static uint32_t observed = 0;
  struct Handler {
    static void Fn(const kernel::event::Source &, const kernel::event::Code &, const kernel::DataBuffer &buffer) {
      observed = buffer ? *buffer.as<uint32_t>() : 0u;
    }
  };

  auto buffer = kernel::DataBuffer::Alloc(sizeof(uint32_t));
  REQUIRE(buffer);
  *buffer.as<uint32_t>() = 0xABCDu;

  auto token = RegisterInterest(
      kernel::event::Source(source),
      kernel::event::RegistrationContext{.fn = &Handler::Fn, .user_buffer = kstd::move(buffer)});

  kernel::event::broadcast(source, Ev(1u));
  PumpAll();
  REQUIRE(observed == 0xABCDu);
}
