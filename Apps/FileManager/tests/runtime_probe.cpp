#include <userspace.h>
#include <undos/syscalls.h>
#include <undoscrt/string.h>

#define CHECK(expression) do { if (!(expression)) { (void) UD_SYS_PrintV1("FileManager probe: FAIL " #expression); return 1; } } while (false)

extern "C" int main() {
  HANDLE directory{}, source{}, copy{};
  FileInformation information{};
  uint32_t found = 0, length = 0;
  CHECK(UD_SYS_ReadDirectoryV1(UINT32_MAX, 0, nullptr, &length, &information, &found) == SyscallReturnCode_INVALID_ARGUMENT);
  CHECK(UD_SYS_GetFileInformationV1(UINT32_MAX, &information) == SyscallReturnCode_INVALID_ARGUMENT);
  CHECK(UD_SYS_DeleteFileV1("/") == SyscallReturnCode_INVALID_ARGUMENT);
  CHECK(UD_SYS_CreateDirectoryV1("relative") == SyscallReturnCode_INVALID_ARGUMENT);
  CHECK(UD_SYS_CopyFileV1("/source", "relative") == SyscallReturnCode_INVALID_ARGUMENT);
  CHECK(UD_SYS_MoveFileV1("relative", "/destination") == SyscallReturnCode_INVALID_ARGUMENT);
  CHECK(UD_SYS_OpenV1("/System", FileOpenMode_Read, &directory) == SyscallReturnCode_OKAY);
  CHECK(UD_SYS_GetFileInformationV1(directory, &information) == SyscallReturnCode_OKAY);
  CHECK(information.attributes & 16);
  CHECK(UD_SYS_ReadDirectoryV1(directory, 0, nullptr, &length, &information, &found) == SyscallReturnCode_OKAY);
  CHECK(found == 1 && length > 1 && length <= 256);
  char guard[] = {'x', 'y'};
  length = 1;
  CHECK(UD_SYS_ReadDirectoryV1(directory, 0, guard, &length, &information, &found) == SyscallReturnCode_INVALID_SIZE);
  CHECK(guard[0] == 'x' && guard[1] == 'y');
  char name[256]{};
  length = sizeof(name);
  CHECK(UD_SYS_ReadDirectoryV1(directory, 0, name, &length, &information, &found) == SyscallReturnCode_OKAY);
  CHECK(found == 1 && name[0] && name[length - 1] == 0);
  CHECK(UD_SYS_CloseV1(directory) == SyscallReturnCode_OKAY);
  CHECK(UD_SYS_CreateDirectoryV1("/FMPROBE") == SyscallReturnCode_OKAY);
  CHECK(UD_SYS_CopyFileV1("/System/PROGMAN.INI", "/FMPROBE/COPY.INI") == SyscallReturnCode_OKAY);
  CHECK(UD_SYS_OpenV1("/System/PROGMAN.INI", FileOpenMode_Read, &source) == SyscallReturnCode_OKAY);
  CHECK(UD_SYS_OpenV1("/FMPROBE/COPY.INI", FileOpenMode_Read, &copy) == SyscallReturnCode_OKAY);
  CHECK(UD_SYS_GetFileInformationV1(source, &information) == SyscallReturnCode_OKAY);
  const uint64_t size = information.size;
  CHECK(UD_SYS_GetFileInformationV1(copy, &information) == SyscallReturnCode_OKAY);
  CHECK(information.size == size);
  uint64_t total = 0;
  for (;;) {
    uint8_t a[512]{}, b[512]{};
    uint32_t n = sizeof(a), m = sizeof(b);
    CHECK(UD_SYS_ReadV1(source, a, &n) == SyscallReturnCode_OKAY);
    CHECK(UD_SYS_ReadV1(copy, b, &m) == SyscallReturnCode_OKAY);
    CHECK(n == m);
    CHECK(!memcmp(a, b, n));
    total += n;
    if (!n) break;
  }
  CHECK(total == size);
  CHECK(UD_SYS_CloseV1(source) == SyscallReturnCode_OKAY);
  CHECK(UD_SYS_CloseV1(copy) == SyscallReturnCode_OKAY);
  CHECK(UD_SYS_MoveFileV1("/FMPROBE/COPY.INI", "/FMPROBE/RENAMED.INI") == SyscallReturnCode_OKAY);
  CHECK(UD_SYS_CreateDirectoryV1("/FMPROBE/SUBDIR") == SyscallReturnCode_OKAY);
  CHECK(UD_SYS_MoveFileV1("/FMPROBE/RENAMED.INI", "/FMPROBE/SUBDIR/MOVED.INI") == SyscallReturnCode_OKAY);
  CHECK(UD_SYS_DeleteFileV1("/FMPROBE") != SyscallReturnCode_OKAY);
  CHECK(UD_SYS_DeleteFileV1("/FMPROBE/SUBDIR/MOVED.INI") == SyscallReturnCode_OKAY);
  CHECK(UD_SYS_DeleteFileV1("/FMPROBE/SUBDIR") == SyscallReturnCode_OKAY);
  CHECK(UD_SYS_DeleteFileV1("/FMPROBE") == SyscallReturnCode_OKAY);
  (void) UD_SYS_PrintV1("FileManager probe: PASS");
  return 0;
}
