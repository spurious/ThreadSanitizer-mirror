// TODO: license!
extern "C" {
  #include "valgrind.h"
  #include "pub_tool_basics.h"
  #include "pub_tool_mallocfree.h"
  #include "pub_tool_libcbase.h"
  #include "pub_tool_libcassert.h"
}

#include <new>
void *operator new (size_t size) {
    return (void*)VG_(malloc)((HChar*)"operator new", size);
}
void *operator new [](size_t size) {
    return (void*)VG_(malloc)((HChar*)"operator new[]", size);
}
void operator delete (void *p) {
    VG_(free)(p);
}
void operator delete [](void *p) {
    VG_(free)(p);
}
void * memmove ( void * destination, const void * source, size_t num ) {
  return VG_(memmove)(destination, source, num);
}
void * memchr (const void * ptr, int value, size_t num ) {
  char chr_value = (char)value,
       *chr_ptr  = (char*)ptr;
  for (size_t i = 0; i < num; i++)
    if (chr_value == chr_ptr[i])
      return chr_ptr + i;
  return NULL;
}
size_t strlen ( const char * str ) {
  return VG_(strlen)((const Char*)str);
}
namespace std {
  void abort() {
    tl_assert(false);
  }
}
