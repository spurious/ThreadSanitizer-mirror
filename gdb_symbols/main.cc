// Copyright 2011 Google Inc. All Rights Reserved.
// Author: glider@google.com (Alexander Potapenko)

#include "symbol_table.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  SymbolTable *st = new SymbolTable();
//  st->MapSharedLibrary(argv[0], strlen(argv[0]), 0x400000);
  st->MapBinary(argv[0], strlen(argv[0]));
  char symbol[100], file[1000];
  int line;
  void *addresses[] = {    (void*)main, (void*)malloc
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
