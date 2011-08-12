#include "bfd_symbolizer.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <string>
#include <dlfcn.h>
#include <vector>

int foo1_line = __LINE__; extern "C" void foo1(int, int) {}
int foo2_line = __LINE__; void foo2() {}
int foo3_line = __LINE__; void foo3(std::string const&, int) {}
int foo5_line = __LINE__; template<typename, int> void foo5(int) {};

extern "C" int foo4; int foo4 = 0;
int foo6 = 0;
static int foo8 = 0;
namespace { int foo9 = 0; }
namespace foo { int foo10 = 0; }
int foo11 [4096];

extern int dyn1_line; extern "C" void* dyn1(int, int);
extern int dyn2_line; extern int dyn2; void* get_dyn2();


void check(void* addr, bfds_opts_e opts, char const* symbol, char const* module, char const* file, int line, int offset, int bufsize = 4096) {
  char buf [4096];
  std::vector<char> symbol0 (bufsize);
  std::vector<char> module0 (bufsize);
  std::vector<char> file0 (bufsize);
  int line0 = -1;
  int offset0 = -1;

  snprintf(buf, sizeof(buf), "'%s' %s:%d", symbol, file, line);
  printf("%-40s...", buf);
  if (bfds_symbolize(addr, opts, &symbol0[0], symbol0.size(), &module0[0], module0.size(), &file0[0], file0.size(), &line0, &offset0)) {
    printf("bfds_symbolize() failed\n");
    exit(1);
  }

  if (strcmp(symbol, &symbol0[0])) {
    printf("symbol: '%s'/'%s'\n", symbol, &symbol0[0]);
    exit(1);
  }

  if (strstr(&module0[0], module) == 0) {
    printf("module: '%s'/'%s'\n", module, &module0[0]);
    exit(1);
  }

  if (strstr(&file0[0], file) == 0) {
    printf("file: '%s'/'%s'\n", file, &file0[0]);
    exit(1);
  }

  if (line != line0) {
    printf("line: %d/%d\n", line, line0);
    exit(1);
  }

  if (offset != offset0) {
    printf("offset: %d/%d\n", offset, offset0);
    exit(1);
  }

  printf("OK\n");
}

int barbaz(void** stack, int size) {
  return bfds_unwind(stack, size, 0);
}

int foobar(void** stack, int size) {
  return barbaz(stack, size);
}

void test_stack_unwind() {
  printf("%-40s...", "unwind");
  void* stack [64];
  int sz = foobar(stack, 64);

  char const* exp[] = {"barbaz", "foobar", "test_stack_unwind", "main"};
  int const cnt = sizeof(exp)/sizeof(*exp);
  if (sz < cnt) {
    printf("not enough stack entries: %d/%d\n", sz, cnt);
    exit(1);
  }

  for (int i = 0; i < cnt; i++) {
    char buf [1024];
    if (bfds_symbolize(stack[i], bfds_opt_demangle, buf, sizeof(buf), 0, 0, 0, 0, 0, 0)) {
      printf("symbolize failed: %d\n", i);
      exit(1);
    }
    if (strcmp(buf, exp[i])) {
      printf("invalid stack entry: %s/%s\n", buf, exp[i]);
      exit(1);
    }
  }
  printf("OK\n");
}


int main() {
#ifdef __x86_64__
  char const* exename = "/test64";
  char const* staname = "/libsta64.so";
  char const* dynname = "libdyn64.so";
#else
  char const* exename = "/test32";
  char const* staname = "/libsta32.so";
  char const* dynname = "libdyn32.so";
#endif

  if (bfds_symbolize((void*)&foo1, bfds_opt_none, 0, 0, 0, 0, 0, 0, 0, 0)) {
    printf("bfds_symbolize(%d) failed\n", __LINE__);
    exit(1);
  }

  if (bfds_symbolize((void*)&foo1, bfds_opt_demangle, 0, 0, 0, 0, 0, 0, 0, 0)) {
    printf("bfds_symbolize(%d) failed\n", __LINE__);
    exit(1);
  }

  if (0 == bfds_symbolize(0, bfds_opt_none, 0, 0, 0, 0, 0, 0, 0, 0)) {
    printf("bfds_symbolize(%d) failed\n", __LINE__);
    exit(1);
  }

  if (0 == bfds_symbolize(malloc(0), bfds_opt_none, 0, 0, 0, 0, 0, 0, 0, 0)) {
    printf("bfds_symbolize(%d) failed\n", __LINE__);
    exit(1);
  }

  check((void*)&foo1,   bfds_opt_none, "foo1",          exename,      __FILE__,      foo1_line, 0);
  check((void*)&foo2,   bfds_opt_none, "_Z4foo2v",      exename,      __FILE__,      foo2_line, 0);
  check((void*)&foo3,   bfds_opt_none, "_Z4foo3RKSsi",  exename,      __FILE__,      foo3_line, 0);

  check((char*)&foo4,   bfds_opt_data, "foo4",          exename,      "",            0,         0);
  check((char*)&foo6,   bfds_opt_data, "foo6",          exename,      "",            0,         0);
  check((char*)&foo8,   bfds_opt_data, "_ZL4foo8",      exename,      "",            0,         0);
  check((char*)&foo9,   bfds_opt_data, "_ZN12_GLOBAL__N_14foo9E",     exename,      "",            0,         0);
  check((char*)&foo::foo10,  bfds_opt_data, "_ZN3foo5foo10E",         exename,      "",            0,         0);
  check((char*)&foo11,  bfds_opt_data, "foo11",         exename,      "",            0,         0);

  check((char*)&foo4,   (bfds_opts_e)(bfds_opt_data | bfds_opt_demangle), "foo4",          exename,      "",            0,         0);
  check((char*)&foo6,   (bfds_opts_e)(bfds_opt_data | bfds_opt_demangle), "foo6",          exename,      "",            0,         0);
  check((char*)&foo8,   (bfds_opts_e)(bfds_opt_data | bfds_opt_demangle), "foo8",      exename,      "",            0,         0);
  check((char*)&foo9,   (bfds_opts_e)(bfds_opt_data | bfds_opt_demangle), "(anonymous namespace)::foo9",     exename,      "",            0,         0);
  check((char*)&foo::foo10,  (bfds_opts_e)(bfds_opt_data | bfds_opt_demangle), "foo::foo10",         exename,      "",            0,         0);
  check((char*)&foo11,  (bfds_opts_e)(bfds_opt_data | bfds_opt_demangle), "foo11",         exename,      "",            0,         0);

  check((char*)&foo4+1, bfds_opt_data, "foo4",          exename,      "",            0,         1);
  check((char*)&foo4+3, bfds_opt_data, "foo4",          exename,      "",            0,         3);

  check((void*)&foo1,   bfds_opt_demangle, "foo1",      exename,      __FILE__,      foo1_line, 0);
  check((void*)&foo2,   bfds_opt_demangle, "foo2",      exename,      __FILE__,      foo2_line, 0);
  check((void*)&foo2,   bfds_opt_demangle_params,  "foo2()",     exename,      __FILE__,      foo2_line, 0);
  check((void*)&foo2,   bfds_opt_demangle_verbose, "foo2()",     exename,      __FILE__,      foo2_line, 0);

  check((void*)&foo3,   bfds_opt_demangle, "foo3",  exename,      __FILE__,      foo3_line, 0);
  check((void*)&foo3,   bfds_opt_demangle_params, "foo3(std::string const&, int)",  exename,      __FILE__,      foo3_line, 0);
  check((void*)&foo3,   bfds_opt_demangle_verbose, "foo3(std::basic_string<char, std::char_traits<char>, std::allocator<char> > const&, int)",  exename,      __FILE__,      foo3_line, 0);

  check((void*)(void(*)(int))&foo5<float, 5>,   bfds_opt_none, "_Z4foo5IfLi5EEvi",  exename,      __FILE__,      foo5_line, 0);
  check((void*)(void(*)(int))&foo5<float, 5>,   bfds_opt_demangle, "foo5<float, 5>",  exename,      __FILE__,      foo5_line, 0);
  check((void*)(void(*)(int))&foo5<float, 5>,   bfds_opt_demangle_params, "void foo5<float, 5>(int)",  exename,      __FILE__,      foo5_line, 0);
  check((void*)(void(*)(int))&foo5<float, 5>,   bfds_opt_demangle_verbose, "void foo5<float, 5>(int)",  exename,      __FILE__,      foo5_line, 0);

  check((void*)&foo2,   bfds_opt_none, "_",      "/",      "/",      foo2_line, 0, 2);
  check((void*)&foo2,   bfds_opt_demangle, "_",      "/",      "/",      foo2_line, 0, 2);

  check(dyn1(0, 0),     bfds_opt_none, "dyn1",          staname, "test_dyn.cc", dyn1_line, 0);
  check(get_dyn2(),     bfds_opt_data, "dyn2",          staname, "",            0,         0);

  check((void*)&dyn1,   bfds_opt_none, "dyn1@plt",      exename, "",            0, 0);
  
  void* dl = dlopen(dynname, RTLD_LOCAL | RTLD_NOW);
  void* dyn21 = dlsym(dl, "dyn21");
  void* dyn22 = dlsym(dl, "dyn22");
  check(dyn21,     bfds_opt_none, "dyn21",          dynname, "test_dyn2.cc", 1, 0);
  check(dyn22,     bfds_opt_data, "dyn22",          "", "", 0, 0);
  dlclose(dl);
  if (bfds_symbolize(dyn21, bfds_opt_none, 0, 0, 0, 0, 0, 0, 0, 0)) {
    printf("bfds_symbolize(%d) failed\n", __LINE__);
    exit(1);
  }
  if (bfds_symbolize(dyn22, bfds_opt_data, 0, 0, 0, 0, 0, 0, 0, 0)) {
    printf("bfds_symbolize(%d) failed\n", __LINE__);
    exit(1);
  }

  if (0 == bfds_symbolize(dyn21, bfds_opt_update_libs, 0, 0, 0, 0, 0, 0, 0, 0)) {
    printf("bfds_symbolize(%d) failed\n", __LINE__);
    exit(1);
  }
  if (0 == bfds_symbolize(dyn22, (bfds_opts_e)(bfds_opt_update_libs | bfds_opt_data), 0, 0, 0, 0, 0, 0, 0, 0)) {
    printf("bfds_symbolize(%d) failed\n", __LINE__);
    exit(1);
  }
 
  test_stack_unwind();
 
  printf("OK\n");
  return 0;
}






