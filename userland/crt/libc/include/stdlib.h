#pragma once

#include <stddef.h>
#include <undoscrt/process.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#ifdef __cplusplus
extern "C" {
#endif
void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *memory, size_t size);
void free(void *memory);
int abs(int value);
long labs(long value);
long long llabs(long long value);
int atoi(const char *text);
long atol(const char *text);
long strtol(const char *text, char **end, int base);
unsigned long strtoul(const char *text, char **end, int base);
char *getenv(const char *name);
int system(const char *command);
#ifdef __cplusplus
[[noreturn]] void abort(void);
}
#else
_Noreturn void abort(void);
#endif
