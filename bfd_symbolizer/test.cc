#include "bfd_symbolizer.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <string>


int foo1_line = __LINE__; extern "C" void foo1(int, int) {}
int foo2_line = __LINE__; void foo2() {}
int foo3_line = __LINE__; void foo3(std::string const&, int) {}


void check(void* addr, bfds_opts opts, char const* symbol, char const* module, char const* file, int line, int offset, int is_func) {
  char buf [4096];
  char symbol0 [4096];
  char module0 [4096];
  char file0 [4096];
  int line0 = -1;
  int offset0 = -1;
  int is_func0 = -1;

  snprintf(buf, sizeof(buf), "'%s' %s:%d", symbol, file, line);
  printf("%-40s...", buf);
  if (bfds_symbolize(addr, opts, symbol0, sizeof(symbol0), module0, sizeof(module0), file0, sizeof(file0), &line0, &offset0, &is_func0)) {
    printf("bfds_symbolize() failed\n");
    exit(1);
  }

  if (strcmp(symbol, symbol0)) {
    printf("symbol: '%s'/'%s'\n", symbol, symbol0);
    exit(1);
  }

  if (strcmp(module, module0)) {
    printf("module: '%s'/'%s'\n", module, module0);
    exit(1);
  }

  if (strstr(file0, file) == 0) {
    printf("file: '%s'/'%s'\n", file, file0);
    exit(1);
  }

  if (line != line0) {
    printf("line: %d/%d\n", line, line0);
    exit(1);
  }

/*
  if (offset != offset0) {
    printf("offset: %d/%d\n", offset, offset0);
    exit(1);
  }

  if (is_func != is_func0) {
    printf("is_func: %d/%d\n", is_func, is_func0);
    exit(1);
  }
*/

  printf("OK\n");
}


int main() {
  char exename [1024];
  readlink("/proc/self/exe", exename, sizeof(exename));

  check((void*)&foo1, bfds_opt_none, "foo1", exename, __FILE__, foo1_line, 0, 1);
  check((void*)&foo2, bfds_opt_none, "_Z4foo2v", exename, __FILE__, foo2_line, 0, 1);

  return 0;
}


