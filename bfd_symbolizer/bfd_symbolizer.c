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
#include <assert.h>
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

#ifdef _DEBUG
# define ERR(...) fprintf(stderr, "BFD: " __VA_ARGS__)
# define DBG(...) fprintf(stderr, "DBG: " __VA_ARGS__)
#else
# define ERR(...)
# define DBG(...)
#endif


typedef struct var_t {
  void*                 addr;
  char const*           name;
} var_t;


typedef struct lib_t {
  struct lib_t*         next;
  char*                 name;
  void*                 begin;
  void*                 end;
  struct bfd*           bfd;
  asymbol**             syms;
	int                   symcount;
	asymbol**             dynsyms;
	int                   dynsymcount;
	asymbol*              synsyms;
	int                   synsymcount;
  var_t*                vars;
  int                   var_count;
  int                   is_seen;
  int                   is_main_exec;
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


typedef struct sym_t {
  lib_t*                lib;
  bfd_vma               pc;
  char const*           filename;
  char const*           functionname;
  unsigned int          line;
  int                   offset;
  bfd_boolean           found;
} sym_t;


static struct ctx_t ctx = {PTHREAD_MUTEX_INITIALIZER};


static int var_sort_pred(void const* p1, void const* p2) {
  return (intptr_t)(((var_t*)p1)->addr) - (intptr_t)(((var_t*)p2)->addr);
}


static int var_search_pred(void const* p1, void const* p2) {
  var_t*                v0;
  var_t*                v1;
  var_t*                v2;

  v0 = (var_t*)p1;
  v1 = (var_t*)p2;
  v2 = v1 + 1;
  if (v0->addr < v1->addr)
    return -1;
  if (v0->addr >= v2->addr)
    return 1;
  return 0;
}


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


static int parse_lib(char* pos, void** lbegin, void** lend, char** lname) {
  char*                 end;

  *lbegin = (void*)(uintptr_t)strtoull(pos, &end, 16);
  if (end[0] != '-')
    return 1;
  pos = end + 1;
  *lend = (void*)(uintptr_t)strtoull(pos, &end, 16);
  if (end[0] != ' ')
    return 1;
  pos = strchr(end, '/');
  if (pos == 0 && strchr(end, '[') != 0)
    return 1;
  *lname = pos;
  return 0;
}


static lib_t* lib_alloc(void* lbegin, void* lend, char const* lname) {
  lib_t*                lib;

  lib = (lib_t*)malloc(sizeof(lib_t));
  if (lib == 0)
    return 0;
  lib->name = strdup(lname);
  if (lib->name == 0) {
    free(lib);
    return 0;
  }
  lib->begin = lbegin;
  lib->end = lend;
  lib->bfd = 0;
  lib->syms = 0;
	lib->symcount = 0;
	lib->dynsyms = 0;
	lib->dynsymcount = 0;
	lib->synsyms = 0;
	lib->synsymcount = 0;
  lib->vars = 0;
  lib->var_count = 0;
  lib->is_seen = 1;
  lib->is_main_exec = 0;
  lib->next = 0;
  return lib;
}


static void lib_free(lib_t* lib) {
  if (lib->bfd != 0)
    bfd_close(lib->bfd);
  free(lib->vars);
  free(lib->syms);
	free(lib->dynsyms);
	free(lib->synsyms);
  free(lib->name);
  free(lib);
}


static lib_t* update_lib(lib_t* prev, void* lbegin, void* lend, char const* lname) {
  lib_t*                lib;

  DBG("found module '%s' at %p-%p\n", lname, lbegin, lend);
  if (prev != 0 && (lname == 0 || strcmp(prev->name, lname) == 0)) {
    assert(lbegin >= prev->end);
    prev->end = lend;
    return lname == 0 ? 0 : prev;
  }

  if (lname == 0)
    return 0;

  for (lib = ctx.libs; lib != 0; lib = lib->next) {
    if (strcmp(lib->name, lname) == 0)
      break;
  }

  if (lib == 0 || lib->is_seen != 0) {
    lib = lib_alloc(lbegin, lend, lname);
    if (lib == 0)
      return prev;
    if (ctx.libs == 0)
      lib->is_main_exec = 1;
    lib->next = ctx.libs;
    ctx.libs = lib;
  } else {
    assert(lib != prev);
    lib->is_seen = 1;
    lib->begin = lbegin;
    lib->end = lend;
  }
  return lib;
}


static void update_libs_impl(char* data, int sz) {
  uint32_t              crc;
  char*                 pos;
  char*                 next_line;
  lib_t*                lib;
  lib_t**               lprev;
  lib_t*                lnext;
  void*                 lbegin;
  void*                 lend;
  char*                 lname;

  crc = crc32(data, sz);
  if (crc == ctx.maps_crc) {
    DBG("CRC32 is not changed\n");
    return;
  }

  DBG("/proc/self/maps raw:\n%s\n", data);
  ctx.maps_crc = crc;
  for (lib = ctx.libs; lib != 0; lib = lib->next) {
    lib->is_seen = 0;
  }

  lib = 0;
  pos = data;
  for (;;) {
    next_line = strchr(pos, '\n');
    if (next_line == 0)
      break;
    *next_line = 0;
    if (parse_lib(pos, &lbegin, &lend, &lname) == 0) {
      lib = update_lib(lib, lbegin, lend, lname);
    }
    pos = next_line + 1;
  }

  lprev = &ctx.libs;
  for (lib = ctx.libs; lib != 0; lib = lnext) {
    lnext = lib->next;
    if (lib->is_seen == 0) {
      DBG("dropping module '%s'\n", lib->name);
      *lprev = lnext;
      lib_free(lib);
    } else {
      lprev = &lib->next;
    }
  }

  DBG("refresh completed\n");
  DBG("module list:\n");
  for (lib = ctx.libs; lib != 0; lib = lib->next) {
    DBG("%p-%p %s\n", lib->begin, lib->end, lib->name);
  }
}


static void update_libs() {
  int                   f;
  int                   fsz;
  off_t                 res;
  char*                 data;

  DBG("refreshing /proc/self/maps\n");
  f = open("/proc/self/maps", O_RDONLY);
  if (f == -1) {
    ERR("open(\"/proc/self/maps\") failed (%s)\n",
        strerror(errno));
    return;
  }

  if (ctx.maps_size == 0)
    ctx.maps_size = 128;

  fsz = 0;
  data = (char*)malloc(ctx.maps_size);
  for (;;) {
    if (data == 0) {
      ERR("malloc(%d) failed (%s)\n",
          ctx.maps_size, strerror(errno));
      close(f);
      return;
    }
    res = read(f, data + fsz, ctx.maps_size - fsz);
    DBG("read %u\n", (int)res);
    if (res == (off_t)-1) {
      ERR("read('/proc/self/maps') failed (%s)\n",
          strerror(errno));
      free(data);
      close(f);
      return;
    }
    if (res == 0) {
      data[fsz] = 0;
      break;
    }
    fsz += res;
    if (fsz == ctx.maps_size) {
      ctx.maps_size *= 2;
      data = (char*)realloc(data, ctx.maps_size);
    }
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

  if (lib != 0) {
    DBG("found lib '%s' %p-%p\n",
        lib->name, lib->begin, lib->end);
  } else {
    DBG("lib is not found\n");
  }

  return lib;
}


static int init_lib(lib_t* lib) {
  char**                matching;
  unsigned              symsize;
  long                  i;
  long                  j;

  if (ctx.is_bfd_init == 0) {
    ctx.is_bfd_init = 1;
    bfd_init();
  }

  DBG("initializing bfd for module '%s'\n", lib->name);
  lib->bfd = bfd_openr(lib->name, 0);
  if (lib->bfd == 0) {
    ERR("bfd_openr(%s) failed: %s\n",
        lib->name, bfd_errmsg(bfd_get_error()));
    return 1;
  }

  if (bfd_check_format(lib->bfd, bfd_archive)) {
    ERR("bfd_check_format(%s) failed: %s\n",
        lib->name, bfd_errmsg(bfd_get_error()));
    bfd_close(lib->bfd);
    lib->bfd = 0;
    return 1;
  }

  matching = 0;
  if (!bfd_check_format_matches(lib->bfd, bfd_object, &matching)) {
    ERR("bfd_check_format_matches(%s) failed: %s\n",
        lib->name, bfd_errmsg(bfd_get_error()));
    bfd_close(lib->bfd);
    lib->bfd = 0;
    return 1;
  }
  free(matching);

  if ((bfd_get_file_flags(lib->bfd) & HAS_SYMS) == 0) {
    ERR("bfd_get_file_flags(%s) failed: %s\n",
        lib->name, bfd_errmsg(bfd_get_error()));
    bfd_close(lib->bfd);
    lib->bfd = 0;
    return 1;
  }

  lib->symcount = bfd_read_minisymbols(lib->bfd, 0, (void**)&lib->syms, &symsize);
	if (lib->symcount < 0)
		lib->symcount = 0;
  lib->dynsymcount = bfd_read_minisymbols(lib->bfd, 1, (void**)&lib->dynsyms, &symsize);
	if (lib->dynsymcount < 0)
		lib->dynsymcount = 0;
  if (lib->symcount == 0 && lib->dynsymcount == 0) {
    ERR("bfd_read_minisymbols(%s) failed: %s\n",
        lib->name, bfd_errmsg(bfd_get_error()));
		free(lib->syms);
		free(lib->dynsyms);
		lib->syms = 0;
		lib->dynsyms = 0;
    bfd_close(lib->bfd);
    lib->bfd = 0;
    return 1;
  }

  lib->var_count = 0;
  for (i = 0; i != lib->symcount; i += 1) {
    if ((lib->syms[i]->flags & BSF_OBJECT) != 0
        && (lib->syms[i]->flags & BSF_THREAD_LOCAL) == 0)
      lib->var_count += 1;
  }
  lib->vars = (var_t*)malloc((lib->var_count + 1) * sizeof(var_t));
  if (lib->vars == 0) {
    free(lib->syms);
		free(lib->dynsyms);
    lib->syms = 0;
		lib->dynsyms = 0;
    bfd_close(lib->bfd);
    lib->bfd = 0;
    return 1;
  }

  for (i = 0, j = 0; i != lib->symcount; i += 1) {
    if ((lib->syms[i]->flags & BSF_OBJECT) != 0
        && (lib->syms[i]->flags & BSF_THREAD_LOCAL) == 0) {
      lib->vars[j].addr = (void*)bfd_asymbol_value(lib->syms[i]);
      lib->vars[j].name = bfd_asymbol_name(lib->syms[i]);
      j += 1;
    }
  }
  lib->vars[j].addr = (void*)-1;
  lib->vars[j].name = "STUB";
  qsort(lib->vars, lib->var_count, sizeof(var_t), var_sort_pred);

	lib->synsymcount = bfd_get_synthetic_symtab(lib->bfd,
																							lib->symcount,
																							lib->syms,
																							lib->dynsymcount,
																							lib->dynsyms,
																							&lib->synsyms);
  DBG("symbols:\n");
  for (i = 0; i != lib->symcount; i += 1) {
    DBG("\t%p: %s%s%s%s%s%s%s%s%s%s%s%s%s%s\n",
        (void*)bfd_asymbol_value(lib->syms[i]),
        bfd_asymbol_name(lib->syms[i]),
        (lib->syms[i]->flags & BSF_LOCAL ? " LOCAL" : ""),
        (lib->syms[i]->flags & BSF_GLOBAL ? " GLOBAL" : ""),
        (lib->syms[i]->flags & BSF_DEBUGGING ? " DEBUG" : ""),
        (lib->syms[i]->flags & BSF_FUNCTION ? " FUNC" : ""),
        (lib->syms[i]->flags & BSF_WEAK ? " WEAK" : ""),
        (lib->syms[i]->flags & BSF_SECTION_SYM ? " SECTION" : ""),
        (lib->syms[i]->flags & BSF_CONSTRUCTOR ? " CONSTRUCTOR" : ""),
        (lib->syms[i]->flags & BSF_INDIRECT ? " INDIRECT" : ""),
        (lib->syms[i]->flags & BSF_FILE ? " FILE" : ""),
        (lib->syms[i]->flags & BSF_DYNAMIC ? " DYNAMIC" : ""),
        (lib->syms[i]->flags & BSF_OBJECT ? " OBJECT" : ""),
        (lib->syms[i]->flags & BSF_DEBUGGING_RELOC ? " DEBUG_RELOC" : ""),
        (lib->syms[i]->flags & BSF_THREAD_LOCAL ? " THREAD_LOCAL" : ""));
  }
	
  DBG("vars:\n");
  for (i = 0; i != lib->var_count + 1; i += 1) {
    DBG("\t%p: %s\n",
        lib->vars[i].addr,
        lib->vars[i].name);
  }

	DBG("synsyms:\n");
	for (i = 0; i != lib->synsymcount; i += 1) {
		DBG("\t%p: %s\n",
				(void*)bfd_asymbol_value(&lib->synsyms[i]),
        bfd_asymbol_name(&lib->synsyms[i]));
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


static void bfd_search_callback(bfd* abfd, asection* section, void* data) {
  bfd_vma               vma;
  bfd_size_type         size;
  sym_t*                psi;


  psi = (sym_t*)data;
  if (psi->found)
    return;

  if ((bfd_get_section_flags(abfd, section) & SEC_ALLOC) == 0)
    return;

  vma = bfd_get_section_vma(abfd, section);
  size = bfd_get_section_size(section);
  if (psi->pc < vma || psi->pc >= vma + size)
    return;

  psi->found = bfd_find_nearest_line(psi->lib->bfd, section,
                                     psi->lib->syms, psi->pc - vma,
                                     &psi->filename, &psi->functionname,
                                     &psi->line);
}


static int process_data(lib_t* lib, void* addr, char* symbol, int symbol_size, char* filename, int filename_size, int* source_line, int* symbol_offset) {
  var_t*                v;
  var_t                 v0;

  if (lib->is_main_exec == 0) {
    addr = (char*)addr - (ptrdiff_t)lib->begin;
    DBG("shifting addr to %p\n", addr);
  }
  v0.addr = addr;

  v = (var_t*)bsearch(&v0, lib->vars, lib->var_count, sizeof(var_t), var_search_pred);
  if (v == 0)
    return 1;
  assert(v->addr <= addr);
  strcopy(symbol, symbol_size, v->name);
  if (symbol_offset != 0)
    *symbol_offset = (char*)addr - (char*)v->addr;
  strcopy(filename, filename_size, "");
  if (source_line != 0)
    *source_line = 0;
  return 0;
}


static int process_code(lib_t* lib, void* addr, char* symbol, int symbol_size, char* filename, int filename_size, int* source_line, int* symbol_offset) {
  sym_t                 si;
  char*                 alloc;
  const char*           name;
	int                   i;

  DBG("resolving address %p in module '%s'\n", addr, lib->name);
  if (lib->is_main_exec == 0) {
    addr = (char*)addr - (ptrdiff_t)lib->begin;
    DBG("shifting to %p\n", addr);
  }

  memset(&si, 0, sizeof(si));
  si.lib = lib;
  si.pc = (bfd_vma)addr;
  si.found = 0;
  bfd_map_over_sections(lib->bfd, bfd_search_callback, &si);
  if (si.found == 0) {
		for (i = 0; i != lib->synsymcount; i += 1) {
			if (addr == (void*)bfd_asymbol_value(&lib->synsyms[i])) {
				strcopy(symbol, symbol_size, bfd_asymbol_name(&lib->synsyms[i]));
				strcopy(filename, filename_size, "");
				if (source_line != 0)
					*source_line = 0;
				if (symbol_offset != 0)
					*symbol_offset = 0;
				return 0;
			}
		}
    return 1;
	}

  do {
    alloc = 0;
    name = si.functionname;
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
  if (symbol_offset != 0)
    *symbol_offset = 0;
  return 0;
}


static int process_demangle(char* symbol, int symbol_size, bfds_opts_e opts) {
  char*                 demangled;
  int                   dopts;

  if ((opts & bfds_opt_demangle) == 0
      && (opts & bfds_opt_demangle_params) == 0
      && (opts & bfds_opt_demangle_verbose) == 0)
    return 0;

  if (symbol_size == 0
      || symbol == 0
      || symbol[0] == 0)
    return 0;
  
  dopts = DMGL_AUTO;
  if ((opts & bfds_opt_demangle_params) != 0)
    dopts |= DMGL_PARAMS;
  if ((opts & bfds_opt_demangle_verbose) != 0)
    dopts |= DMGL_PARAMS | DMGL_VERBOSE;

  demangled = cplus_demangle(symbol, dopts);
  if (demangled == 0)
    return 0;

  DBG("demangled '%s'->'%s'\n", symbol, demangled);

  strcopy(symbol, symbol_size, demangled);
  free(demangled);
  return 0;
}


int   bfds_symbolize    (void*                  addr,
                         bfds_opts_e            opts,
                         char*                  symbol,
                         int                    symbol_size,
                         char*                  module,
                         int                    module_size,
                         char*                  filename,
                         int                    filename_size,
                         int*                   source_line,
                         int*                   symbol_offset) {
  lib_t*                lib;

  pthread_mutex_lock(&ctx.mtx);

  DBG("request for addr %p (%s)\n", addr, (opts & bfds_opt_data ? "data" : "code"));

  if (process_lib(&lib, addr, opts & bfds_opt_update_libs, module, module_size)) {
    ERR("module for address %p is not found\n", addr);
    pthread_mutex_unlock(&ctx.mtx);
    return 1;
  }

  if (opts & bfds_opt_data) {
    if (process_data(lib, addr, symbol, symbol_size, filename, filename_size, source_line, symbol_offset)) {
      ERR("symbol for data address %p is not found\n", addr);
      pthread_mutex_unlock(&ctx.mtx);
      return 1;
    }
  } else {
    if (process_code(lib, addr, symbol, symbol_size, filename, filename_size, source_line, symbol_offset)) {
      ERR("symbol for code address %p is not found\n", addr);
      pthread_mutex_unlock(&ctx.mtx);
      return 1;
    }
  }

  if (process_demangle(symbol, symbol_size, opts)) {
    ERR("demangling for address %p is failed\n", addr);
    pthread_mutex_unlock(&ctx.mtx);
    return 1;
  }

  DBG("addr %p: module='%s', symbol='%s', file='%s', line=%d, offset=%d\n",
      addr, module, symbol, filename,
      source_line ? *source_line : -1,
      symbol_offset ? *symbol_offset : -1);

  pthread_mutex_unlock(&ctx.mtx);
  return 0;
}









