#include <windows.h>
#include <undoscrt/stdio.h>
#include <undoscrt/string.h>

/* Lists the root directory with the kernel32 file API, the way WINFILE's
   directory reader does, and shows FindExecutable for an .ELF file. */
int main(void) {
  WIN32_FIND_DATA data;
  HANDLE find = FindFirstFile("/*.*", &data);
  if (find == INVALID_HANDLE_VALUE) {
    crt_printf("files: FindFirstFile failed error=%u\n", (unsigned)GetLastError());
    return 1;
  }
  unsigned count = 0;
  do {
    crt_printf("files: %s%s size=%u attr=0x%x\n", data.cFileName,
               (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? "\\" : "",
               (unsigned)data.nFileSizeLow, (unsigned)data.dwFileAttributes);
    ++count;
  } while (FindNextFile(find, &data));
  FindClose(find);
  crt_printf("files: total=%u\n", count);
  char roots[256];
  if (GetLogicalDriveStrings(sizeof(roots), roots)) {
    for (const char *root = roots; *root; root += strlen(root) + 1) {
      char label[32] = "", fileSystem[16] = "";
      DWORD serial = 0;
      GetVolumeInformation(root, label, sizeof(label), &serial, NULL, NULL,
                           fileSystem, sizeof(fileSystem));
      crt_printf("files: mount %s type=%u fs=%s label=%s serial=%08x\n", root,
                 GetDriveType(root), fileSystem, label, (unsigned)serial);
    }
  }
  return 0;
}
