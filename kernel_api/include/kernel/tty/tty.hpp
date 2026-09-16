#pragma once

#include <stddef.h>
#include <stdint.h>
#include <atomic.hpp>
#include <mutex.hpp>
#include <kernel/__core.hpp>
#include <kernel/mutex.hpp>
#include <kernel/fwd/KPtr.hpp>

namespace kernel::tty {

struct WinSize {
  uint16_t ws_row{25};
  uint16_t ws_col{80};
  uint16_t ws_xpixel{0};
  uint16_t ws_ypixel{0};
};

enum TtyFlags : uint32_t {
  TTY_ECHO       = 1 << 0,  // Echo typed characters back to source
  TTY_CANONICAL  = 1 << 1,  // Canonical (cooked) mode vs raw mode
  TTY_CR_TO_NL   = 1 << 2,  // Map \r to \n on input
  TTY_NL_TO_CRNL = 1 << 3,  // Map \n to \r\n on output
  TTY_ECHO_NL    = 1 << 4,  // Echo newline even if ECHO is off
};

struct TtyConfig {
  uint32_t flags{TTY_ECHO | TTY_CANONICAL | TTY_CR_TO_NL | TTY_NL_TO_CRNL};
  WinSize size{};
};

constexpr size_t kDefaultBufferSize = 4096;
constexpr size_t kMaxEditBufferSize = 1024;

template <typename T, size_t Capacity>
class RingBuffer {
public:
  static constexpr size_t kCapacity = Capacity;

  constexpr RingBuffer() noexcept = default;

  [[nodiscard]] bool empty() const noexcept { return m_count == 0; }
  [[nodiscard]] bool full() const noexcept { return m_count == kCapacity; }
  [[nodiscard]] size_t size() const noexcept { return m_count; }
  [[nodiscard]] size_t capacity() const noexcept { return kCapacity; }
  [[nodiscard]] size_t available() const noexcept { return kCapacity - m_count; }

  void clear() noexcept {
    m_head = 0;
    m_tail = 0;
    m_count = 0;
  }

  bool push(const T &item) noexcept {
    if (m_count == kCapacity) {
      return false;
    }
    m_data[m_tail] = item;
    m_tail = (m_tail + 1) % kCapacity;
    ++m_count;
    return true;
  }

  bool pop(T &item) noexcept {
    if (m_count == 0) {
      return false;
    }
    item = m_data[m_head];
    m_head = (m_head + 1) % kCapacity;
    --m_count;
    return true;
  }

  size_t write(const T *src, size_t count) noexcept {
    size_t written = 0;
    while (written < count && m_count < kCapacity) {
      m_data[m_tail] = src[written++];
      m_tail = (m_tail + 1) % kCapacity;
      ++m_count;
    }
    return written;
  }

  size_t read(T *dst, size_t count) noexcept {
    size_t readCount = 0;
    while (readCount < count && m_count > 0) {
      dst[readCount++] = m_data[m_head];
      m_head = (m_head + 1) % kCapacity;
      --m_count;
    }
    return readCount;
  }

private:
  T m_data[Capacity]{};
  size_t m_head{0};
  size_t m_tail{0};
  size_t m_count{0};
};

class KVtty {
public:
  KVtty();
  ~KVtty() = default;

  KVtty(const KVtty &) = delete;
  KVtty &operator=(const KVtty &) = delete;

  // Source endpoint operations
  size_t source_write(const uint8_t *src, size_t count);
  size_t source_read(uint8_t *dst, size_t count);
  [[nodiscard]] bool source_has_data() const;
  void close_source();
  [[nodiscard]] bool is_source_closed() const;

  // Sink endpoint operations
  size_t sink_write(const uint8_t *src, size_t count);
  size_t sink_read(uint8_t *dst, size_t count);
  [[nodiscard]] bool sink_has_data() const;
  void close_sink();
  [[nodiscard]] bool is_sink_closed() const;

  // Configuration and window size
  [[nodiscard]] TtyConfig get_config() const;
  void set_config(const TtyConfig &config);
  [[nodiscard]] uint32_t get_flags() const;
  void set_flags(uint32_t flags);
  [[nodiscard]] WinSize get_winsize() const;
  void set_winsize(const WinSize &ws);

  // Line discipline reset / clear
  void flush();

  // Intrusive reference counting
  void retain() noexcept {
    m_refCount.fetch_add(1, kstd::memory_order_relaxed);
  }

  void release() noexcept {
    if (m_refCount.fetch_sub(1, kstd::memory_order_acq_rel) == 1) {
      kstd::atomic_thread_fence(kstd::memory_order_acquire);
      delete this;
    }
  }

  [[nodiscard]] uint32_t ref_count() const noexcept {
    return m_refCount.load(kstd::memory_order_relaxed);
  }

private:
  kstd::atomic<uint32_t> m_refCount{0};
  mutable kernel::Mutex m_lock;
  RingBuffer<uint8_t, kDefaultBufferSize> m_sinkInRing;   // Input available for sink to read
  RingBuffer<uint8_t, kDefaultBufferSize> m_sourceOutRing; // Output available for source to read
  uint8_t m_editBuffer[kMaxEditBufferSize]{};
  size_t m_editLen{0};

  TtyConfig m_config{};
  bool m_sourceClosed{false};
  bool m_sinkClosed{false};
  bool m_eofSignaled{false};
};

} // namespace kernel::tty

namespace kernel::tty {

struct VttyRefTraits {
  using base_type = KVtty;
  template<typename T>
  constexpr static void inc_ref(T *ref) { if (ref) ref->retain(); }
  template<typename T>
  constexpr static void dec_ref(T *ref) { if (ref) ref->release(); }
  template<typename T>
  constexpr static T *release(T *ptr) { return ptr; }
  template<typename T, typename U>
  constexpr static bool convertible_to(T *) { return true; }
};

using VttyPtr = detail::KPtr<KVtty, VttyRefTraits>;

} // namespace kernel::tty

namespace kernel {
using KVtty = tty::KVtty;
using VttyPtr = tty::VttyPtr;
}
