#include <kernel/tty/tty.hpp>

namespace kernel::tty {

KVtty::KVtty() = default;

size_t KVtty::source_write(const uint8_t *src, size_t count) {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  if (m_sourceClosed || m_sinkClosed) {
    return 0;
  }

  size_t processed = 0;
  for (size_t i = 0; i < count; ++i) {
    uint8_t ch = src[i];

    // Carriage return translation (\r -> \n)
    if ((m_config.flags & TTY_CR_TO_NL) && ch == '\r') {
      ch = '\n';
    }

    if (!(m_config.flags & TTY_CANONICAL)) {
      // Raw mode: pass byte directly to sink in ring
      if (!m_sinkInRing.full()) {
        m_sinkInRing.push(ch);
        if (m_config.flags & TTY_ECHO) {
          if ((m_config.flags & TTY_NL_TO_CRNL) && ch == '\n') {
            const uint8_t crnl[2] = {'\r', '\n'};
            m_sourceOutRing.write(crnl, 2);
          } else {
            m_sourceOutRing.push(ch);
          }
        }
        processed++;
      } else {
        break;
      }
      continue;
    }

    // Canonical (cooked) mode
    processed++;
    if (ch == '\b' || ch == 0x7F) {
      if (m_editLen > 0) {
        m_editLen--;
        if (m_config.flags & TTY_ECHO) {
          const uint8_t eraseSeq[3] = {'\b', ' ', '\b'};
          m_sourceOutRing.write(eraseSeq, 3);
        }
      }
    } else if (ch == 0x15) {
      // Ctrl+U (line kill)
      if (m_config.flags & TTY_ECHO) {
        const uint8_t eraseSeq[3] = {'\b', ' ', '\b'};
        for (size_t k = 0; k < m_editLen; ++k) {
          m_sourceOutRing.write(eraseSeq, 3);
        }
      }
      m_editLen = 0;
    } else if (ch == 0x04) {
      // Ctrl+D (EOF)
      if (m_editLen > 0) {
        m_sinkInRing.write(m_editBuffer, m_editLen);
        m_editLen = 0;
      } else {
        m_eofSignaled = true;
      }
    } else if (ch == '\n') {
      if (m_editLen < kMaxEditBufferSize) {
        m_editBuffer[m_editLen++] = '\n';
      } else {
        m_editBuffer[kMaxEditBufferSize - 1] = '\n';
      }
      if ((m_config.flags & TTY_ECHO) || (m_config.flags & TTY_ECHO_NL)) {
        if (m_config.flags & TTY_NL_TO_CRNL) {
          const uint8_t crnl[2] = {'\r', '\n'};
          m_sourceOutRing.write(crnl, 2);
        } else {
          m_sourceOutRing.push('\n');
        }
      }
      m_sinkInRing.write(m_editBuffer, m_editLen);
      m_editLen = 0;
    } else {
      if (m_editLen < kMaxEditBufferSize) {
        m_editBuffer[m_editLen++] = ch;
        if (m_config.flags & TTY_ECHO) {
          m_sourceOutRing.push(ch);
        }
      }
    }
  }

  return processed;
}

size_t KVtty::source_read(uint8_t *dst, size_t count) {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  if (count == 0) return 0;
  return m_sourceOutRing.read(dst, count);
}

bool KVtty::source_has_data() const {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  return !m_sourceOutRing.empty();
}

void KVtty::close_source() {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  m_sourceClosed = true;
}

bool KVtty::is_source_closed() const {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  return m_sourceClosed;
}

size_t KVtty::sink_write(const uint8_t *src, size_t count) {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  if (m_sinkClosed || m_sourceClosed) {
    return 0;
  }

  size_t written = 0;
  for (size_t i = 0; i < count; ++i) {
    uint8_t ch = src[i];
    if ((m_config.flags & TTY_NL_TO_CRNL) && ch == '\n') {
      if (m_sourceOutRing.available() < 2) {
        break;
      }
      m_sourceOutRing.push('\r');
      m_sourceOutRing.push('\n');
      written++;
    } else {
      if (!m_sourceOutRing.push(ch)) {
        break;
      }
      written++;
    }
  }
  return written;
}

size_t KVtty::sink_read(uint8_t *dst, size_t count) {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  if (count == 0) return 0;

  if (m_sinkInRing.empty()) {
    if (m_eofSignaled) {
      m_eofSignaled = false;
      return 0;
    }
    if (m_sourceClosed) {
      return 0;
    }
    return 0;
  }

  return m_sinkInRing.read(dst, count);
}

bool KVtty::sink_has_data() const {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  return !m_sinkInRing.empty();
}

void KVtty::close_sink() {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  m_sinkClosed = true;
}

bool KVtty::is_sink_closed() const {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  return m_sinkClosed;
}

TtyConfig KVtty::get_config() const {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  return m_config;
}

void KVtty::set_config(const TtyConfig &config) {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  m_config = config;
}

uint32_t KVtty::get_flags() const {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  return m_config.flags;
}

void KVtty::set_flags(uint32_t flags) {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  m_config.flags = flags;
}

WinSize KVtty::get_winsize() const {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  return m_config.size;
}

void KVtty::set_winsize(const WinSize &ws) {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  m_config.size = ws;
}

void KVtty::flush() {
  kstd::unique_lock<kernel::Mutex> lock(m_lock);
  m_sinkInRing.clear();
  m_sourceOutRing.clear();
  m_editLen = 0;
  m_eofSignaled = false;
}

} // namespace kernel::tty
