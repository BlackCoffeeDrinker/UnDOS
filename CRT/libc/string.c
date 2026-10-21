#include <string.h>
#include <errno.h>

void *memchr(const void *memory, int ch, size_t size) {
  const unsigned char *bytes = memory;
  for (size_t i = 0; i < size; ++i)
    if (bytes[i] == (unsigned char)ch) return (void *)(bytes + i);
  return NULL;
}
char *strncat(char *destination, const char *source, size_t size) {
  char *out = destination + strlen(destination);
  while (size-- && *source) *out++ = *source++;
  *out = '\0';
  return destination;
}
char *strstr(const char *text, const char *needle) {
  const size_t size = strlen(needle);
  if (!size) return (char *)text;
  for (; *text; ++text)
    if (!strncmp(text, needle, size)) return (char *)text;
  return NULL;
}
size_t strspn(const char *text, const char *accept) {
  size_t size = 0;
  while (text[size] && strchr(accept, text[size])) ++size;
  return size;
}
size_t strcspn(const char *text, const char *reject) {
  size_t size = 0;
  while (text[size] && !strchr(reject, text[size])) ++size;
  return size;
}
char *strpbrk(const char *text, const char *accept) {
  const size_t offset = strcspn(text, accept);
  return text[offset] ? (char *)(text + offset) : NULL;
}
char *strtok(char *text, const char *delimiters) {
  static char *next;
  if (!text) text = next;
  if (!text) return NULL;
  text += strspn(text, delimiters);
  if (!*text) { next = NULL; return NULL; }
  char *end = text + strcspn(text, delimiters);
  next = *end ? end + 1 : NULL;
  if (*end) *end = '\0';
  return text;
}
char *strerror(int error) {
  const char *message;
  switch (error) {
    case 0: message = "No error"; break;
    case EIO: message = "Input/output error"; break;
    case EBADF: message = "Invalid stream handle"; break;
    case ENOMEM: message = "Out of memory"; break;
    case EINVAL: message = "Invalid argument"; break;
    case EMFILE: message = "Too many open streams"; break;
    case ERANGE: message = "Result out of range"; break;
    case ENOSYS: message = "Operation not supported"; break;
    case EOVERFLOW: message = "Value too large"; break;
    default: message = "Unknown error"; break;
  }
  return (char *)message;
}
