#include <undoscrt/stdio.h>
#include <undoscrt/string.h>
#include <stdbool.h>

#include <userspace.h>
#include <undos/syscalls.h>

static HANDLE g_standard_handles[3];
static bool g_standard_handle_available[3];

// Formatting overflow fails rather than allocating a larger buffer.
#define OUTPUT_BUFFER_SIZE 512

static int WriteStdout(const char *data, size_t length) {
  HANDLE handle = 0;
  if (length > INT32_MAX ||
      crt_get_std_handle(StdHandleSelector_Stdout, &handle) != 0) return -1;
  if (length == 0) return 0;
  const uint32_t rc = UD_SYS_WriteV1(handle, (const uint8_t *)data, (uint32_t)length);
  return rc == SyscallReturnCode_OKAY ? (int)length : -1;
}

// Renders `value` in `base` (10 or 16) into `out`, returns the number of
// characters written. `out` must have room for at least 32 characters.
static size_t FormatUnsigned(uint32_t value, unsigned base, bool uppercase, char *out) {
  const char *kLowerDigits = "0123456789abcdef";
  const char *kUpperDigits = "0123456789ABCDEF";
  const char *digits = uppercase ? kUpperDigits : kLowerDigits;

  char scratch[32];
  size_t len = 0;
  do {
    scratch[len++] = digits[value % base];
    value /= base;
  } while (value != 0);

  for (size_t i = 0; i < len; ++i) {
    out[i] = scratch[len - 1 - i];
  }
  return len;
}

static size_t FormatSigned(int32_t value, char *out) {
  if (value < 0) {
    out[0] = '-';
    return 1 + FormatUnsigned(0u - (uint32_t)value, 10, false, out + 1);
  }
  return FormatUnsigned((uint32_t)value, 10, false, out);
}

int crt_initialize_stdio(void) {
  const enum StdHandleSelector selectors[] = {
      StdHandleSelector_Stdin,
      StdHandleSelector_Stdout,
      StdHandleSelector_Stderr,
  };
  bool allAvailable = true;
  for (uint32_t i = 0; i < 3; ++i) {
    HANDLE handle = 0;
    const uint32_t rc = UD_SYS_GetStdHandleV1(selectors[i], &handle);
    g_standard_handle_available[i] = rc == SyscallReturnCode_OKAY;
    if (g_standard_handle_available[i]) {
      g_standard_handles[i] = handle;
    } else {
      g_standard_handles[i] = 0;
      allAvailable = false;
    }
  }
  return allAvailable ? 0 : -1;
}

int crt_get_std_handle(enum StdHandleSelector selector, HANDLE *handle) {
  const uint32_t index = (uint32_t)selector;
  if (!handle || index >= 3 || !g_standard_handle_available[index]) return -1;
  *handle = g_standard_handles[index];
  return 0;
}

int crt_puts(const char *message) {
  if (!message) return -1;
  return WriteStdout(message, strlen(message));
}

int crt_getchar(void) {
  HANDLE handle = 0;
  if (crt_get_std_handle(StdHandleSelector_Stdin, &handle) != 0) return -1;

  uint8_t ch = 0;
  while (true) {
    uint32_t bytesRead = 1;
    const uint32_t rc = UD_SYS_ReadV1(handle, &ch, &bytesRead);
    if (rc != SyscallReturnCode_OKAY) {
      return -1;
    }
    if (bytesRead > 0) {
      return (int)ch;
    }
  }
}

char *crt_readline(char *buffer, size_t size) {
  if (!buffer || size == 0) return NULL;

  size_t idx = 0;
  while (idx < size - 1) {
    const int c = crt_getchar();
    if (c < 0) {
      if (idx == 0) return NULL;
      break;
    }
    buffer[idx++] = (char)c;
    if (c == '\n') {
      break;
    }
  }
  buffer[idx] = '\0';
  return buffer;
}

typedef struct {
  char buffer[OUTPUT_BUFFER_SIZE];
  size_t pos;
  bool truncated;
} CrtOutput;

static void put_char(CrtOutput *out, char ch) {
  if (out->pos < OUTPUT_BUFFER_SIZE - 1) out->buffer[out->pos++] = ch;
  else out->truncated = true;
}
static void put_string(CrtOutput *out, const char *text) {
  while (*text) put_char(out, *text++);
}

int crt_vprintf(const char *fmt, va_list args) {
  if (!fmt) return -1;
  CrtOutput out = {0};

  for (const char *p = fmt; *p; ++p) {
    if (*p != '%') {
      put_char(&out, *p);
      continue;
    }

    ++p;
    char digits[32];
    switch (*p) {
      case 's': {
        const char *s = va_arg(args, const char *);
        put_string(&out, s ? s : "(null)");
        break;
      }
      case 'c':
        put_char(&out, (char)va_arg(args, int));
        break;
      case 'd': {
        const size_t len = FormatSigned(va_arg(args, int32_t), digits);
        for (size_t i = 0; i < len; ++i) put_char(&out, digits[i]);
        break;
      }
      case 'u': {
        const size_t len = FormatUnsigned(va_arg(args, uint32_t), 10, false, digits);
        for (size_t i = 0; i < len; ++i) put_char(&out, digits[i]);
        break;
      }
      case 'x': {
        const size_t len = FormatUnsigned(va_arg(args, uint32_t), 16, false, digits);
        for (size_t i = 0; i < len; ++i) put_char(&out, digits[i]);
        break;
      }
      case 'p': {
        put_string(&out, "0x");
        const size_t len = FormatUnsigned((uintptr_t)va_arg(args, void *), 16, false, digits);
        for (size_t i = 0; i < len; ++i) put_char(&out, digits[i]);
        break;
      }
      case '%':
        put_char(&out, '%');
        break;
      case '\0':
        --p;// avoid stepping past the terminator in the for-loop increment
        break;
      default:
        put_char(&out, '%');
        put_char(&out, *p);
        break;
    }
  }

  if (out.truncated) return -1;
  out.buffer[out.pos] = '\0';
  return WriteStdout(out.buffer, out.pos);
}

int crt_printf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const int result = crt_vprintf(fmt, args);
  va_end(args);
  return result;
}
