#include "tsan_interface.h"
#include <stdio.h>

int main() {
  __tsan_init();
  printf("tsan_c test PASSED\n");
  return 0;
}
