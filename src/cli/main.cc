/*
 File: main.cc

 ncx-cli: NCX without a window, for tests and debugging. For now:

   ncx-cli --version     the version
   ncx-cli --selftest    check the libraries (exit 0 if all is well)
 */

#include "base/Core.h"

#include <cstdio>
#include <cstring>
#include <string>

int main(int argc, char * argv[])
{
  if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
    std::printf("ncx-cli %s\n", ncx::Version());
    return 0;
  }
  if (argc == 2 && std::strcmp(argv[1], "--selftest") == 0) {
    std::string report;
    bool ok = ncx::SelfTest(report);
    std::fputs(report.c_str(), stdout);
    return ok ? 0 : 1;
  }
  std::fprintf(stderr, "Usage: ncx-cli --version | --selftest\n");
  return 2;
}
