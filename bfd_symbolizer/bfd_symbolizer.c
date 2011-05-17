/** BFD-based Symbolizer
 *  Copyright (c) 2011, Google Inc. All rights reserved.
 *  Author: Dmitry Vyukov (dvyukov)
 *
 *  It is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 3, or (at your option) any later
 *  version. See http://www.gnu.org/licenses/
 */

#include "bfd_symbolizer.h"
#include <pthread.h>
#include <demangle.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>


typedef struct lib_t {
  struct lib_t*         next;
  char const*           name;
  void*                 begin;
  void*                 end;
} lib_t;


typedef struct ctx_t {
  pthread_mutex_t       mtx;
  lib_t*                libs;
  uint32_t              maps_crc;
  uint32_t              crc_tab [256];
  int                   is_crc_init;
} ctx_t;


static struct ctx_t ctx = {PTHREAD_MUTEX_INITIALIZER};


static void strcopy(char* dst, int dstsize, char const* src) {
  if (dst != 0 && dstsize != 0 && src != 0)
    snprintf(dst, dstsize, "%s", src);
}


static uint32_t crc32(char const* data, int sz) {
  long unsigned         crc;
  long unsigned         poly;
  int                   i;
  int                   j;

  if (ctx.is_crc_init == 0) {
    ctx.is_crc_init = 1;
    poly = 0xEDB88320L;
    for (i = 0; i != 256; i += 1) {
      crc = i;
      for (j = 8; j > 0; j--) {
        if (crc & 1)
          crc = (crc >> 1) ^ poly;
        else
          crc >>= 1;
      }
      ctx.crc_tab[i] = crc;
    }
  }

  crc = 0xFFFFFFFF;
  for (i = 0; i != sz; i += 1)
    crc = ((crc >> 8) & 0x00FFFFFF) ^ ctx.crc_tab[(crc ^ *data++) & 0xFF];
  return (crc ^ 0xFFFFFFFF);
}


static void update_libs_impl(char const* data, int sz) {
  uint32_t              crc;

  crc = crc32(data, sz);
  if (crc == ctx.maps_crc)
    return;
}


static void update_libs() {
  int                   f;
  off_t                 fsz;
  char*                 data;

  f = open("/proc/self/maps", O_RDONLY);
  if (f == -1)
    return;

  fsz = lseek(f, 0, SEEK_END);
  if (fsz == 0 || fsz == (off_t)-1) {
    close(f);
    return;
  }

  data = (char*)malloc(fsz + 1);
  if (data == 0) {
    close(f);
    return;
  }

  lseek(f, 0, SEEK_SET);
  fsz = read(f, data, fsz);
  if (fsz == 0 || fsz == (off_t)-1) {
    free(data);
    close(f);
    return;
  }

  data[fsz] = 0;
  update_libs_impl(data, fsz);
  free(data);
  close(f);
}


static lib_t* find_lib(void* addr) {
  lib_t*                lib;

  for (lib = ctx.libs; lib != 0; lib = lib->next) {
    if (addr >= lib->begin && addr < lib->end)
      break;
  }
  return lib;
}


static int process_lib(lib_t** plib, void* addr, int do_update_libs, char* module, int module_size) {
  lib_t*                lib;

  if (do_update_libs) {
    update_libs();
    lib = find_lib(addr);
  } else {
    lib = find_lib(addr);
    if (lib == 0) {
      update_libs();
      lib = find_lib(addr);
    }
  }

  if (lib == 0)
    return 1;

  strcopy(module, module_size, lib->name);
  *plib = lib;
  return 0;
}


static int process_symb(lib_t* lib, void* addr, char* symbol, int symbol_size, char* filename, int filename_size, int* source_line, int* symbol_offset, int* is_function) {
  return 0;
}


static int process_demangle(char* symbol, int symbol_size, bfds_opts opts) {
  char*                 demangled;
  int                   dopts;

  if ((opts & bfds_opt_demangle) == 0
      || symbol_size == 0
      || symbol == 0
      || symbol[0] == 0)
    return 0;
  
  dopts = DMGL_AUTO;
  if ((opts & bfds_opt_func_params) != 0)
    dopts |= DMGL_PARAMS | DMGL_RET_POSTFIX;
  if ((opts & bfds_opt_templates) != 0)
    dopts |= DMGL_VERBOSE;

  demangled = cplus_demangle(symbol, dopts);
  if (demangled == 0)
    return 0;

  strcopy(symbol, symbol_size, demangled);
  free(demangled);
  return 0;
}


int   bfds_symbolize    (void*                  addr,
                         bfds_opts              opts,
                         char*                  symbol,
                         int                    symbol_size,
                         char*                  module,
                         int                    module_size,
                         char*                  filename,
                         int                    filename_size,
                         int*                   source_line,
                         int*                   symbol_offset,
                         int*                   is_function) {
  lib_t*                lib;

  pthread_mutex_lock(&ctx.mtx);

  if (process_lib(&lib, addr, opts & bfds_opt_update_libs, module, module_size)) {
    pthread_mutex_unlock(&ctx.mtx);
    return 1;
  }

  if (process_symb(lib, addr, symbol, symbol_size, filename, filename_size, source_line, symbol_offset, is_function)) {
    pthread_mutex_unlock(&ctx.mtx);
    return 1;
  }

  if (process_demangle(symbol, symbol_size, opts)) {
    pthread_mutex_unlock(&ctx.mtx);
    return 1;
  }

  pthread_mutex_unlock(&ctx.mtx);
  return 0;
}




