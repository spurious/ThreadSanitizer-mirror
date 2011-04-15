// Copyright 2011 Google Inc. All Rights Reserved.
// Author: glider@google.com (Alexander Potapenko)

#include "symbol_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

int GLOB = 0;

int main(int argc, char *argv[]) {
  SymbolTable *st = new SymbolTable(argv[0]);
  char symbol[100], file[1000];
  int line;
  typedef void*(malloc_fun)(size_t);
  malloc_fun *malloc_addr = malloc;
  malloc_addr(1);
  void *addresses[] = {
    (void*)main,
    (void*)pthread_create,
    (void*)(&SymbolTable::GetAddrInfoNocache),
    (void*)&GLOB,
    (void*)malloc_addr
  };
  for (int i = 0; i < sizeof(addresses) / sizeof(void*); ++i) {
    void *addr = addresses[i];
    if (st->GetAddrInfoNocache(addr, symbol, sizeof(symbol),
                               file, sizeof(file), &line)) {
      printf("%p is <%s> at line %d of %s\n", addr, symbol, line, file);
    } else {
      printf("symbolization of %p failed\n", addr);
    }
  }
  delete st;
  return 0;
}
