#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
int main() {
  const std::vector<std::string> items{"mPainter", "Windows x64", "MSVC ABI"};
  for (const auto &item : items) std::printf("%s\n", item.c_str());
  std::printf("pointer_bits=%zu pid=%lu\n", sizeof(void *) * 8, GetCurrentProcessId());
  return sizeof(void *) == 8 ? 0 : 1;
}
