#include "../model.hpp"

using namespace filemanager;

#define CHECK(expression) do { if (!(expression)) return __LINE__; } while (false)

int main() {
  char path[PathCapacity]{};
  CHECK(canonical("/", path) && compare(path, "/") == 0);
  CHECK(canonical("//System/./Apps/../", path) && compare(path, "/System") == 0);
  CHECK(canonical("/../../System", path) && compare(path, "/System") == 0);
  CHECK(!canonical("relative", path));
  CHECK(!canonical("/bad\\name", path));
  CHECK(!canonical("/bad\nname", path));
  CHECK(join("/", "FILE.TXT", path) && compare(path, "/FILE.TXT") == 0);
  CHECK(join("/SYSTEM", "FILE.TXT", path) && compare(path, "/SYSTEM/FILE.TXT") == 0);
  CHECK(join("/SYSTEM", "/OTHER", path) && compare(path, "/OTHER") == 0);
  CHECK(!join("/", "", path));
  CHECK(join("/SYSTEM", "../OTHER", path) && compare(path, "/OTHER") == 0);
  parent(path);
  CHECK(compare(path, "/") == 0);
  parent(path);
  CHECK(compare(path, "/") == 0);
  CHECK(canonical("/SYSTEM/PROGRAMS", path));
  parent(path);
  CHECK(compare(path, "/SYSTEM") == 0);
  CHECK(compare(basename(path), "SYSTEM") == 0);
  char maximum[PathCapacity + 1]{};
  maximum[0] = '/';
  for (size_t i = 1; i < PathCapacity; ++i) maximum[i] = 'X';
  CHECK(!canonical(maximum, path));
  maximum[PathCapacity - 1] = 0;
  CHECK(canonical(maximum, path));
  CHECK(!join(maximum, "Y", path));
  char small[4] = "old";
  CHECK(!copy(small, sizeof(small), "toolong"));
  CHECK(compare(small, "old") == 0);
  CHECK(compare("Alpha", "ALPHA") == 0);
  CHECK(compare("A", "AB") < 0);
  CHECK(compare("AB", "A") > 0);
  char number[24]{};
  decimal(0, number);
  CHECK(compare(number, "0") == 0);
  decimal(UINT64_MAX, number);
  CHECK(compare(number, "18446744073709551615") == 0);
  Entry entries[4]{};
  copy(entries[0].name, PathCapacity, "Z.TXT"); entries[0].size = 1;
  copy(entries[1].name, PathCapacity, "FOLDER"); entries[1].attributes = 16;
  copy(entries[2].name, PathCapacity, "A.TXT"); entries[2].size = 9;
  copy(entries[3].name, PathCapacity, "B.TXT"); entries[3].size = 9;
  sort(entries, 4, false, false);
  CHECK(entries[0].directory());
  CHECK(compare(entries[1].name, "A.TXT") == 0);
  sort(entries, 4, true, false);
  CHECK(entries[0].directory());
  CHECK(compare(entries[1].name, "Z.TXT") == 0);
  CHECK(compare(entries[2].name, "A.TXT") == 0);
  sort(entries, 4, true, true);
  CHECK(entries[0].directory());
  CHECK(compare(entries[1].name, "B.TXT") == 0);
  sort(entries, 0, false, false);
  return 0;
}
