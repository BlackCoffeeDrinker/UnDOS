#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <undoscrt/stdio.h>
#include "backend.h"

struct __undos_FILE {
  bool active;
  bool readable;
  bool writable;
  bool eof;
  bool error;
  bool started;
  int standard;
  HANDLE handle;
  int pushed;
  int mode;
  size_t used;
  size_t capacity;
  char *buffer;
  char storage[BUFSIZ];
};

static FILE streams[FOPEN_MAX] = {
    {.active = true, .readable = true, .writable = false, .eof = false, .error = false, .started = false, .standard = 0, .handle = 0, .pushed = EOF, .mode = _IONBF, .used = 0, .capacity = BUFSIZ, .buffer = NULL, .storage = {0}},
    {.active = true, .readable = false, .writable = true, .eof = false, .error = false, .started = false, .standard = 1, .handle = 0, .pushed = EOF, .mode = _IONBF, .used = 0, .capacity = BUFSIZ, .buffer = NULL, .storage = {0}},
    {.active = true, .readable = false, .writable = true, .eof = false, .error = false, .started = false, .standard = 2, .handle = 0, .pushed = EOF, .mode = _IONBF, .used = 0, .capacity = BUFSIZ, .buffer = NULL, .storage = {0}},
};

static int fail(FILE *stream, int error) {
  stream->error = true;
  errno = error;
  return EOF;
}
static bool ready(FILE *stream, bool writing) {
  if (!stream || !stream->active) { errno = EBADF; return false; }
  if (writing ? !stream->writable : !stream->readable) {
    fail(stream, EBADF);
    return false;
  }
  stream->started = true;
  if (stream->standard >= 0 &&
      crt_get_std_handle((enum StdHandleSelector)stream->standard, &stream->handle)) {
    fail(stream, EBADF);
    return false;
  }
  return true;
}
static size_t write_bytes(FILE *stream, const char *data, size_t size) {
  size_t done = 0;
  while (done < size) {
    size_t length = size - done;
    if (length > UNDOS_LIBC_IO_CHUNK_SIZE) length = UNDOS_LIBC_IO_CHUNK_SIZE;
    const size_t requested = length;
    const int error = undos_libc_write(stream->handle, data + done, &length);
    if (error || !length || length > requested) {
      fail(stream, error ? error : EIO);
      break;
    }
    done += length;
  }
  return done;
}
FILE *stdin = &streams[0];
FILE *stdout = &streams[1];
FILE *stderr = &streams[2];

FILE *fopen(const char *path, const char *mode) {
  if (!path || !mode || !*mode) { errno = EINVAL; return NULL; }
  if (strcmp(mode, "r") && strcmp(mode, "rb")) {
    // Creation/truncation and seek semantics are not guaranteed by the VFS yet.
    errno = strchr("rwa", mode[0]) ? ENOSYS : EINVAL;
    return NULL;
  }
  for (size_t i = 3; i < FOPEN_MAX; ++i) {
    if (streams[i].active) continue;
    HANDLE handle = 0;
    const int error = undos_libc_open(path, &handle);
    if (error) { errno = error; return NULL; }
    streams[i] = (FILE) {.active = true, .readable = true, .writable = false, .eof = false, .error = false, .started = false, .standard = -1, .handle = handle, .pushed = EOF, .mode = _IONBF, .used = 0, .capacity = BUFSIZ, .buffer = NULL, .storage = {0}};
    return &streams[i];
  }
  errno = EMFILE;
  return NULL;
}
int fflush(FILE *stream) {
  if (!stream) {
    int result = 0;
    for (size_t i = 0; i < FOPEN_MAX; ++i)
      if (streams[i].active && streams[i].writable && fflush(&streams[i])) result = EOF;
    return result;
  }
  if (!stream->active) { errno = EBADF; return EOF; }
  if (!stream->writable) { errno = ENOSYS; return EOF; }
  if (!stream->used) return 0;
  if (!ready(stream, true)) return EOF;
  const size_t done = write_bytes(stream, stream->buffer, stream->used);
  stream->used -= done;
  if (stream->used) {
    memmove(stream->buffer, stream->buffer + done, stream->used);
    return EOF;
  }
  return 0;
}
int fclose(FILE *stream) {
  if (!stream || !stream->active) { errno = EBADF; return EOF; }
  int result = stream->writable ? fflush(stream) : 0;
  if (stream->standard >= 0 &&
      crt_get_std_handle((enum StdHandleSelector)stream->standard, &stream->handle)) {
    errno = EBADF;
    result = EOF;
  } else {
    const int error = undos_libc_close(stream->handle);
    if (error) { errno = error; result = EOF; }
  }
  stream->active = false;
  return result;
}
int setvbuf(FILE *stream, char *buffer, int mode, size_t size) {
  if (!stream || !stream->active) { errno = EBADF; return -1; }
  if (stream->started || (mode != _IONBF && mode != _IOLBF && mode != _IOFBF) ||
      (mode != _IONBF && !size)) { errno = EINVAL; return -1; }
  if (stream->readable && mode != _IONBF) { errno = ENOSYS; return -1; }
  stream->mode = mode;
  stream->buffer = buffer ? buffer : stream->storage;
  stream->capacity = buffer ? size : (size < BUFSIZ ? size : BUFSIZ);
  return 0;
}
void setbuf(FILE *stream, char *buffer) {
  (void) setvbuf(stream, buffer, buffer ? _IOFBF : _IONBF, BUFSIZ);
}
int feof(FILE *stream) { return stream && stream->eof; }
int ferror(FILE *stream) { return stream && stream->error; }
void clearerr(FILE *stream) { if (stream) { stream->eof = false; stream->error = false; } }
size_t fread(void *buffer, size_t size, size_t count, FILE *stream) {
  if (!size || !count) return 0;
  if (!ready(stream, false)) return 0;
  if (!buffer || count > SIZE_MAX / size) { fail(stream, EINVAL); return 0; }
  if (stream->eof) return 0;
  unsigned char *bytes = buffer;
  const size_t total = size * count;
  size_t done = 0;
  if (stream->pushed != EOF) {
    bytes[done++] = (unsigned char)stream->pushed;
    stream->pushed = EOF;
  }
  while (done < total) {
    size_t length = total - done;
    if (length > UNDOS_LIBC_IO_CHUNK_SIZE) length = UNDOS_LIBC_IO_CHUNK_SIZE;
    const size_t requested = length;
    const int error = undos_libc_read(stream->handle, bytes + done, &length);
    if (error || length > requested) { fail(stream, error ? error : EIO); break; }
    if (!length) { stream->eof = true; break; }
    done += length;
  }
  return done / size;
}
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream) {
  if (!size || !count) return 0;
  if (!ready(stream, true)) return 0;
  if (!buffer || count > SIZE_MAX / size) { fail(stream, EINVAL); return 0; }
  const char *bytes = buffer;
  const size_t total = size * count;
  if (stream->mode == _IONBF) return write_bytes(stream, bytes, total) / size;
  size_t done = 0;
  while (done < total) {
    if (stream->used == stream->capacity && fflush(stream)) break;
    stream->buffer[stream->used++] = bytes[done++];
    if ((stream->used == stream->capacity ||
         (stream->mode == _IOLBF && bytes[done - 1] == '\n')) && fflush(stream))
      return (done - 1) / size;
  }
  return done / size;
}
int fgetc(FILE *stream) {
  unsigned char ch;
  return fread(&ch, 1, 1, stream) == 1 ? ch : EOF;
}
int getc(FILE *stream) { return fgetc(stream); }
int getchar(void) { return fgetc(stdin); }
int ungetc(int ch, FILE *stream) {
  if (!ready(stream, false)) return EOF;
  if (ch == EOF || stream->pushed != EOF) return EOF;
  stream->pushed = (unsigned char)ch;
  stream->eof = false;
  return stream->pushed;
}
char *fgets(char *buffer, int size, FILE *stream) {
  if (!buffer || size <= 0) { errno = EINVAL; return NULL; }
  if (!ready(stream, false)) return NULL;
  int done = 0;
  while (done < size - 1) {
    const int ch = fgetc(stream);
    if (ch == EOF) {
      if (ferror(stream) || !done) return NULL;
      break;
    }
    buffer[done++] = (char)ch;
    if (ch == '\n') break;
  }
  buffer[done] = '\0';
  return buffer;
}
int fputc(int ch, FILE *stream) {
  const unsigned char byte = (unsigned char)ch;
  return fwrite(&byte, 1, 1, stream) == 1 ? byte : EOF;
}
int putc(int ch, FILE *stream) { return fputc(ch, stream); }
int putchar(int ch) { return fputc(ch, stdout); }
int fputs(const char *text, FILE *stream) {
  const size_t size = strlen(text);
  if (!ready(stream, true)) return EOF;
  return fwrite(text, 1, size, stream) == size ? 0 : EOF;
}
int puts(const char *text) {
  if (fputs(text, stdout) == EOF || fputc('\n', stdout) == EOF) return EOF;
  return 0;
}
int fseek(FILE *stream, long offset, int origin) {
  (void)stream; (void)offset; (void)origin;
  errno = ENOSYS;
  return -1;
}
long ftell(FILE *stream) { (void)stream; errno = ENOSYS; return -1; }
void perror(const char *message) {
  const int saved = errno;
  if (message && *message) { fputs(message, stderr); fputs(": ", stderr); }
  fputs(strerror(saved), stderr);
  fputc('\n', stderr);
}
