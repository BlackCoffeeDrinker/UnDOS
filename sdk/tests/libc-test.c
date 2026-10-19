#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include <limits.h>
#include <errno.h>
#include <setjmp.h>

void test_io(int error, int present, size_t limit);
const char *test_output(void);
void test_clear_output(void);
void test_fail_after(size_t count);
void test_input(const char *text);
static jmp_buf exit_jump;
static int exit_code;
_Noreturn void test_terminate(int code) {
  exit_code = code;
  longjmp(exit_jump, 1);
}
int undos_test_app_main(int argc, char **argv) { (void)argc; (void)argv; return 0; }
static void output_at_exit(void) { fputs("callback", stdout); }

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (0)

static int format_args(char *buffer, size_t size, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int result = vsnprintf(buffer, size, format, args);
  va_end(args);
  return result;
}

int main(void) {
  char text[256];
  CHECK(snprintf(text, sizeof(text), "%s %c %d %u %x %X %o %%", "hi", '!', INT_MIN,
                 UINT_MAX, 0xab, 0xab, 9u) > 0);
  CHECK(strstr(text, "hi ! -2147483648 4294967295 ab AB 11 %") == text);
  CHECK(snprintf(text, 4, "abcdef") == 6 && !strcmp(text, "abc"));
  CHECK(snprintf(text, 1, "x") == 1 && text[0] == '\0');
  CHECK(snprintf(NULL, 1, "x") == -1 && errno == EINVAL);
  CHECK(snprintf(NULL, 0, "%s", "count") == 5);
  CHECK(snprintf(NULL, 0, "%0600d", 73) == 600);
  CHECK(snprintf(text, sizeof(text), "%+08d|%-5s|%#x|%#.0o|%.0u|%.3s",
                 -42, "x", 42u, 0u, 0u, "abcdef") == 26);
  CHECK(!strcmp(text, "-0000042|x    |0x2a|0||abc"));
  CHECK(format_args(text, sizeof(text), "%lld %llu", LLONG_MIN, ULLONG_MAX) == 41);
  CHECK(!strcmp(text, "-9223372036854775808 18446744073709551615"));
  CHECK(snprintf(text, sizeof(text), "%*.*d", -7, 3, 4) == 7 && !strcmp(text, "004    "));
  CHECK(snprintf(text, sizeof(text), "%hhd %hhu %zu", 255, 257, (size_t)17) == 7);
  CHECK(!strcmp(text, "-1 1 17"));
  CHECK(snprintf(text, sizeof(text), "%f", 1.0) < 0 && errno == ENOSYS);
  CHECK(snprintf(text, sizeof(text), "%n", (int *)0) < 0 && errno == ENOSYS);
  CHECK(snprintf(text, sizeof(text), "%") < 0 && errno == EINVAL);
  CHECK(snprintf(text, sizeof(text), "%99999999999d", 1) < 0 && errno == EOVERFLOW);

  CHECK(malloc(0) == NULL && calloc(0, 4) == NULL);
  char *memory = malloc(17);
  CHECK(memory && (uintptr_t)memory % 16 == 0);
  strcpy(memory, "retained");
  CHECK(realloc(memory, SIZE_MAX) == NULL && errno == ENOMEM && !strcmp(memory, "retained"));
  char *grown = realloc(memory, 200);
  CHECK(grown && !strcmp(grown, "retained"));
  CHECK(realloc(grown, 0) == NULL);
  CHECK(calloc(SIZE_MAX, 2) == NULL && errno == ENOMEM);
  memory = calloc(7, 3);
  CHECK(memory);
  for (int i = 0; i < 21; ++i) CHECK(memory[i] == 0);
  free(memory);
  CHECK(malloc(1024 * 1024) == NULL && errno == ENOMEM);
  char *end;
  CHECK(strtol(" -0x20tail", &end, 0) == -32 && !strcmp(end, "tail"));
  CHECK(strtol("0x", &end, 0) == 0 && *end == 'x');
  CHECK(strtol("n/a", &end, 10) == 0 && *end == 'n');
  CHECK(strtol("9", &end, 1) == 0 && errno == EINVAL);
  CHECK(strtol("99999999999999999999999999", &end, 10) == LONG_MAX && errno == ERANGE);
  CHECK(strtol("-99999999999999999999999999", &end, 10) == LONG_MIN && errno == ERANGE);
  CHECK(strtoul("-1", &end, 10) == ULONG_MAX);
  snprintf(text, sizeof(text), "-%lu", ULONG_MAX);
  CHECK(strtoul(text, &end, 10) == 1);
  CHECK(atoi(" -73") == -73 && abs(-7) == 7);
  CHECK(!getenv("PATH") && system(NULL) == 0 && system("x") == -1 && errno == ENOSYS);
  strcpy(text, "abc");
  CHECK(!strcmp(strncat(text, "defg", 2), "abcde"));
  CHECK(memchr(text, 'c', 5) == text + 2);
  CHECK(strspn("abc1", "abc") == 3 && strcspn("abc1", "1") == 3);
  CHECK(strpbrk("abc1", "1")[0] == '1');
  strcpy(text, ",one::two");
  CHECK(!strcmp(strtok(text, ",:"), "one") && !strcmp(strtok(NULL, ",:"), "two"));
  CHECK(strtok(NULL, ",:") == NULL);
  CHECK(!strcmp(strerror(ENOSYS), "Operation not supported"));

  CHECK(setvbuf(stdout, NULL, _IOLBF, BUFSIZ) == 0);
  CHECK(fputs("buffered", stdout) >= 0 && !strcmp(test_output(), ""));
  CHECK(puts(" hello") >= 0 && !strcmp(test_output(), "buffered hello\n"));
  test_clear_output();
  CHECK(printf("%s %d", "value", 73) == 8 && !strcmp(test_output(), ""));
  CHECK(fflush(NULL) == 0 && !strcmp(test_output(), "value 73"));
  test_io(EIO, 1, 2);
  CHECK(fputs("pending", stdout) >= 0 && fflush(stdout) == EOF && ferror(stdout));
  test_io(0, 1, 2);
  clearerr(stdout);
  CHECK(fflush(stdout) == 0 && !ferror(stdout));
  test_clear_output();
  test_fail_after(3);
  CHECK(fputs("partial", stdout) >= 0 && fflush(stdout) == EOF && ferror(stdout));
  CHECK(!strcmp(test_output(), "par"));
  test_fail_after(SIZE_MAX);
  CHECK(fflush(stdout) == 0 && !strcmp(test_output(), "partial"));
  CHECK(ferror(stdout)); // Successful I/O does not clear the sticky error indicator.
  CHECK(fprintf(stdout, "%d", 7) == 1);
  clearerr(stdout);
  CHECK(setvbuf(stdout, NULL, _IONBF, 0) != 0 && errno == EINVAL);
  test_io(0, 0, 2);
  CHECK(puts("unavailable") == EOF && ferror(stdout) && errno == EBADF);
  clearerr(stdout);
  CHECK(!ferror(stdout));
  test_io(0, 1, 2);
  CHECK(fwrite("abc", SIZE_MAX, 2, stdout) == 0 && errno == EINVAL);
  clearerr(stdout);
  CHECK(fread(text, 1, 0, stdin) == 0);
  CHECK(fgets(text, sizeof(text), stdin) == text && !strcmp(text, "ab\n"));
  CHECK(getchar() == 'Z' && getchar() == EOF && feof(stdin) && !ferror(stdin));
  CHECK(ungetc('Q', stdin) == 'Q' && !feof(stdin) && getchar() == 'Q');
  CHECK(ungetc(EOF, stdin) == EOF);
  test_io(EIO, 1, 2);
  clearerr(stdin);
  CHECK(getchar() == EOF && ferror(stdin) && !feof(stdin));
  test_io(0, 1, 2);
  FILE *file = fopen("/input", "rb");
  CHECK(file && fread(text, 2, 3, file) == 2 && feof(file));
  CHECK(fseek(file, 0, SEEK_SET) == -1 && errno == ENOSYS);
  CHECK(ftell(file) == -1 && errno == ENOSYS);
  CHECK(fclose(file) == 0);
  CHECK(fopen("/input", "w") == NULL && errno == ENOSYS);
  CHECK(fopen("/missing", "r") == NULL && errno == EIO);
  FILE *files[FOPEN_MAX - 3];
  for (int i = 0; i < FOPEN_MAX - 3; ++i) CHECK((files[i] = fopen("/input", "r")) != NULL);
  CHECK(fopen("/input", "r") == NULL && errno == EMFILE);
  for (int i = 0; i < FOPEN_MAX - 3; ++i) CHECK(fclose(files[i]) == 0);
  CHECK(fputs("wrong direction", stdin) == EOF && errno == EBADF);
  clearerr(stdin);
  CHECK(setvbuf(stderr, NULL, _IONBF, 0) == 0);
  test_io(0, 1, 0);
  CHECK(fputc('x', stderr) == EOF && errno == EIO && ferror(stderr));
  test_io(0, 1, 2);
  clearerr(stderr);
  CHECK(fprintf(stderr, "done %d", 73) == 7);
  CHECK(fflush(stdout) == 0);
  char bulk[5001], copied[5001];
  memset(bulk, 'B', sizeof(bulk) - 1);
  bulk[sizeof(bulk) - 1] = '\0';
  test_clear_output();
  test_io(0, 1, SIZE_MAX);
  CHECK(fwrite(bulk, 1, 5000, stderr) == 5000 && !strcmp(test_output(), bulk));
  test_input(bulk);
  clearerr(stdin);
  CHECK(fread(copied, 1, sizeof(copied), stdin) == 5000 && feof(stdin));
  copied[5000] = '\0';
  CHECK(!strcmp(copied, bulk));
  test_clear_output();
  CHECK(fputs("buffered", stdout) >= 0);
  CHECK(atexit(output_at_exit) == 0);
  if (!setjmp(exit_jump)) exit(73);
  CHECK(exit_code == 73 && !strcmp(test_output(), "bufferedcallback"));
  test_clear_output();
  CHECK(fputs("do not flush", stdout) >= 0);
  if (!setjmp(exit_jump)) _Exit(91);
  CHECK(exit_code == 91 && !strcmp(test_output(), ""));
  return 0;
}
