#include "bfd_symbolizer.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>


int main() {
#ifdef __x86_64__
  char const* soname = "libbfds64.so";
#else
  char const* soname = "libbfds32.so";
#endif

  void* bfds = dlopen(soname, RTLD_NOW);
  if (bfds == 0)
    return printf("failed to load '%s'\n", soname);
  bfds_symbolize_t f = (bfds_symbolize_t)dlsym(bfds, BFDS_SYMBOLIZE_FUNC);
  if (f == 0)
    return printf("failed to resolve '%s'\n", BFDS_SYMBOLIZE_FUNC);
  char symbol [4096];
  if (f((void*)main, bfds_opt_demangle, symbol, sizeof(symbol), 0, 0, 0, 0, 0, 0))
    return printf("failed to resolve symbol\n");
  if (strcmp(symbol, "main"))
    return printf("failed to resolve symbol\n");
  return 0;
}



