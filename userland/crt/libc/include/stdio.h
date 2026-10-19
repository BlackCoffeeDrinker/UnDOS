#pragma once

#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)
#define BUFSIZ 256
#define FOPEN_MAX 16
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2

typedef struct __undos_FILE FILE;

#ifdef __cplusplus
extern "C" {
#endif
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);
int setvbuf(FILE *stream, char *buffer, int mode, size_t size);
void setbuf(FILE *stream, char *buffer);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
int fgetc(FILE *stream);
int getc(FILE *stream);
int getchar(void);
int ungetc(int ch, FILE *stream);
char *fgets(char *buffer, int size, FILE *stream);
int fputc(int ch, FILE *stream);
int putc(int ch, FILE *stream);
int putchar(int ch);
int fputs(const char *text, FILE *stream);
int puts(const char *text);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fseek(FILE *stream, long offset, int origin);
long ftell(FILE *stream);
int printf(const char *format, ...);
int fprintf(FILE *stream, const char *format, ...);
int vprintf(const char *format, va_list args);
int vfprintf(FILE *stream, const char *format, va_list args);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list args);
void perror(const char *message);
#ifdef __cplusplus
}
#endif
