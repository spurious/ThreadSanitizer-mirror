// Copyright 2011 Google Inc. All Rights Reserved.
// Author: glider@google.com (Alexander Potapenko)

#ifndef SYMBOL_TABLE_H_
#define SYMBOL_TABLE_H_

#include <stdint.h>

static const char kGdbPath[] = "/usr/bin/gdb";

class SymbolTable {
 public:
  explicit SymbolTable(const char *binary);
  ~SymbolTable();
  void MapBinary(const char *path, int path_size);
  void MapSharedLibrary(const char *path, int path_size, uintptr_t offset);
  bool GetAddrInfoNocache(void *addr,
                          /*out*/char *symbol, int symbol_size,
                          /*out*/char *file, int file_size,
                          /*out*/int *line);
 protected:
  bool BeforeFork();
  bool AfterForkChild();
  bool AfterForkParent();
 private:
  int OpenPipe();
  void Finalize();
  void WriteHexAddr(uintptr_t addr);
  void ConsumeLines();
  void LoadProcMaps();
  void ProcessProcMapsLine(char *line);
  int ReadBuffer(char *buf, int size);
  // File descriptors used to interact with gdb.
  int gdb_in, gdb_out;
  bool finalized;
  char binary_name[1000];
};

#endif  // SYMBOL_TABLE_H_
