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
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <bfd.h>

#define basename basename2
#include <demangle.h>
#undef basename

#define BFDS_PREFIX "BFD: "

#define DBG(...) fprintf(stderr, "DBG: " __VA_ARGS__)


typedef struct lib_t {
  struct lib_t*         next;
  char*                 name;
  void*                 begin;
  void*                 end;
  struct bfd*           bfd;
  asymbol**             syms;
  int                   is_seen;
} lib_t;


typedef struct ctx_t {
  pthread_mutex_t       mtx;
  lib_t*                libs;
  int                   maps_size;
  uint32_t              maps_crc;
  uint32_t              crc_tab [256];
  int                   is_crc_init;
  int                   is_bfd_init;
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


static int parse_lib(char const* pos, void** lbegin, void** lend, char const** lname, char const** lnameend) {
  char*                 end;

  *lbegin = (void*)strtoull(pos, &end, 16);
  if (end[0] != '-')
    return 1;
  pos = end + 1;
  *lend = (void*)strtoull(pos, &end, 16);
  if (end[0] != ' ')
    return 1;
  if (end[3] != 'x')
    return 1;
  pos = strchr(end, '/');
  if (pos == 0)
    return 1;
  *lname = pos;
  pos = strchr(pos, '\n');
  if (pos == 0)
    return 1;
  *lnameend = pos;
  return 0;
}


static lib_t* lib_alloc(void* lbegin, void* lend, char const* lname, char const* lnameend) {
  lib_t*                lib;

  lib = (lib_t*)malloc(sizeof(lib_t));
  if (lib == 0)
    return 0;
  lib->name = (char*)malloc(lnameend - lname + 1);
  if (lib->name == 0) {
    free(lib);
    return 0;
  }
  memcpy(lib->name, lname, lnameend - lname);
  lib->name[lnameend - lname] = 0;
  lib->begin = lbegin;
  lib->end = lend;
  lib->bfd = 0;
  lib->syms = 0;
  lib->is_seen = 1;
  lib->next = 0;
  return lib;
}


static void lib_free(lib_t* lib) {
  //!!! free other resources
  free(lib);
}


static void update_lib(void* lbegin, void* lend, char const* lname, char const* lnameend) {
  lib_t*                lib;

  for (lib = ctx.libs; lib != 0; lib = lib->next) {
    if (lib->begin == lbegin
        && lib->end == lend
        && strncmp(lib->name, lname, lnameend - lname) == 0)
      break;
  }

  if (lib == 0) {
    lib = lib_alloc(lbegin, lend, lname, lnameend);
    if (lib == 0)
      return;
    lib->next = ctx.libs;
    ctx.libs = lib;
  } else {
    lib->is_seen = 1;
  }
}


static void update_libs_impl(char const* data, int sz) {
  uint32_t              crc;
  char const*           pos;
  lib_t*                lib;
  lib_t**               lprev;
  lib_t*                lnext;
  void*                 lbegin;
  void*                 lend;
  char const*           lname;
  char const*           lnameend;

  crc = crc32(data, sz);
  if (crc == ctx.maps_crc)
    return;

  for (lib = ctx.libs; lib != 0; lib = lib->next) {
    lib->is_seen = 0;
  }

  pos = data;
  for (;;) {
    if (parse_lib(pos, &lbegin, &lend, &lname, &lnameend) == 0) {
      update_lib(lbegin, lend, lname, lnameend);
    }
    pos = strchr(pos, '\n') + 1;
    if (pos == (char*)1)
      break;
  }

  lprev = &ctx.libs;
  for (lib = ctx.libs; lib != 0; lib = lnext) {
    lnext = lib->next;
    if (lib->is_seen == 0) {
      *lprev = lnext;
      lib_free(lib);
    } else {
      lprev = &lib->next;
    }
  }
}


static void update_libs() {
  int                   f;
  int                   fsz;
  char*                 data;

  f = open("/proc/self/maps", O_RDONLY | O_NOATIME);
  if (f == -1) {
    fprintf(stderr, BFDS_PREFIX "open(\"/proc/self/maps\") failed"\
        " (%s)\n", sys_errlist[errno]);
    return;
  }

  if (ctx.maps_size == 0)
    ctx.maps_size = 16*1024;

  data = 0;
  for (;;) {
    data = (char*)realloc(data, ctx.maps_size);
    if (data == 0) {
      fprintf(stderr, BFDS_PREFIX "malloc(%d) failed"\
          " (%s)\n", ctx.maps_size, sys_errlist[errno]);
      close(f);
      return;
    }
    fsz = read(f, data, ctx.maps_size);
    if (fsz == (off_t)-1) {
      fprintf(stderr, BFDS_PREFIX "read(\"/proc/self/maps\") failed"\
          " (%s)\n", sys_errlist[errno]);
      free(data);
      close(f);
      return;
    }
    if (fsz < ctx.maps_size) {
      data[fsz] = 0;
      break;
    }
    ctx.maps_size *= 2;
  }

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


static int init_lib(lib_t* lib) {
  char**                matching;
  unsigned              symsize;
  long                  symcount;

  if (ctx.is_bfd_init == 0) {
    ctx.is_bfd_init = 1;
    bfd_init();
  }

  lib->bfd = bfd_openr(lib->name, 0);
  if (lib->bfd == 0) {
    fprintf(stderr, "bfd_openr(%s) failed: %s\n",
        lib->name, bfd_errmsg(bfd_get_error()));
    return 1;
  }

  if (bfd_check_format(lib->bfd, bfd_archive)) {
    fprintf(stderr, "bfd_check_format(%s) failed: %s\n",
        lib->name, bfd_errmsg(bfd_get_error()));
    bfd_close(lib->bfd);
    lib->bfd = 0;
    return 1;
  }

  matching = 0;
  if (!bfd_check_format_matches(lib->bfd, bfd_object, &matching)) {
    fprintf(stderr, "bfd_check_format_matches(%s) failed: %s\n",
        lib->name, bfd_errmsg(bfd_get_error()));
    bfd_close(lib->bfd);
    lib->bfd = 0;
    return 1;
  }
  free(matching);

  if ((bfd_get_file_flags(lib->bfd) & HAS_SYMS) == 0) {
    fprintf(stderr, "bfd_get_file_flags(%s) failed: %s\n",
        lib->name, bfd_errmsg(bfd_get_error()));
    bfd_close(lib->bfd);
    lib->bfd = 0;
    return 1;
  }

  symcount = bfd_read_minisymbols(lib->bfd, 0, (void**)&lib->syms, &symsize);
  if (symcount == 0)
    symcount = bfd_read_minisymbols(lib->bfd, 1, (void**)&lib->syms, &symsize);
  if (symcount < 0) {
    fprintf(stderr, "bfd_read_minisymbols(%s) failed: %s\n",
        lib->name, bfd_errmsg(bfd_get_error()));
    bfd_close(lib->bfd);
    lib->bfd = 0;
    return 1;
  }

  return 0;
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

  if (lib->bfd == 0) {
    if (init_lib(lib))
      return 1;
  }

  strcopy(module, module_size, lib->name);
  *plib = lib;
  return 0;
}


//!!! refactor
struct BfdSymbol {
  lib_t*                        lib;
  bfd_vma                       pc;
  const char*                   filename;
  const char*                   functionname;
  unsigned int                  line;
  bfd_boolean                   found;
};

static void BfdFindAddressCallback(bfd* abfd,
                                   asection* section,
                                   void* data) {
  bfd_vma vma;
  bfd_size_type size;
  BfdSymbol* psi = (BfdSymbol*)data;

  if (psi->found)
    return;

  if ((bfd_get_section_flags(abfd, section) & SEC_ALLOC) == 0)
    return;

  vma = bfd_get_section_vma(abfd, section);
  if (psi->pc < vma)
    return;

  size = bfd_get_section_size(section);
  if (psi->pc >= vma + size)
    return;

  psi->found = bfd_find_nearest_line(psi->lib->bfd, section,
                                     psi->lib->syms, psi->pc - vma,
                                     &psi->filename, &psi->functionname,
                                     &psi->line);
}


static int process_symb(lib_t* lib, void* xaddr, char* symbol, int symbol_size, char* filename, int filename_size, int* source_line, int* symbol_offset, int* is_function) {
  char addr [(CHAR_BIT/4) * (sizeof(void*)) + 2] = {0};
  sprintf(addr, "%p", xaddr);
  BfdSymbol si = {lib};
  si.pc = bfd_scan_vma (addr, NULL, 16);
  si.found = FALSE;
  bfd_map_over_sections(lib->bfd, BfdFindAddressCallback, &si);
  if (si.found == 0)
    return 1;

  do {
    char* alloc = 0;
    const char* name = si.functionname;
    if (name == 0 || *name == '\0') {
      name = "??";
    } else {
      if (alloc != NULL)
        name = alloc;
    }
    strcopy(symbol, symbol_size, name);
    if (alloc != 0)
      free(alloc);
    strcopy(filename, filename_size, si.filename ?: "?");
    if (source_line != 0)
      *source_line = si.line;
    si.found = bfd_find_inliner_info(lib->bfd,
                                     &si.filename,
                                     &si.functionname,
                                     &si.line);
  } while (si.found);
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
    fprintf(stderr, BFDS_PREFIX "module for address %p is not found\n", addr);
    pthread_mutex_unlock(&ctx.mtx);
    return 1;
  }

  if (process_symb(lib, addr, symbol, symbol_size, filename, filename_size, source_line, symbol_offset, is_function)) {
    fprintf(stderr, BFDS_PREFIX "symbol for address %p is not found\n", addr);
    pthread_mutex_unlock(&ctx.mtx);
    return 1;
  }

  if (process_demangle(symbol, symbol_size, opts)) {
    fprintf(stderr, BFDS_PREFIX "demangling for address %p is failed\n", addr);
    pthread_mutex_unlock(&ctx.mtx);
    return 1;
  }

  pthread_mutex_unlock(&ctx.mtx);
  return 0;
}




