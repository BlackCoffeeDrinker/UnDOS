#pragma once

#include <undoscrt/string.h>

#ifdef __cplusplus
extern "C" {
#endif
void *memchr(const void *memory, int ch, size_t size);
char *strncat(char *destination, const char *source, size_t size);
char *strstr(const char *text, const char *needle);
size_t strspn(const char *text, const char *accept);
size_t strcspn(const char *text, const char *reject);
char *strpbrk(const char *text, const char *accept);
char *strtok(char *text, const char *delimiters);
char *strerror(int error);
#ifdef __cplusplus
}
#endif
