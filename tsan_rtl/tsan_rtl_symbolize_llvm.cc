/* Copyright (c) 2010-2011, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

// Author: glider@google.com (Alexander Potapenko)

#include "tsan_rtl.h"
#include "tsan_rtl_wrap.h"
#include "tsan_rtl_symbolize.h"
#include "thread_sanitizer.h"

#include <sys/mman.h>
#include <elf.h>
#if defined(__GNUC__)
# include <cxxabi.h>  // __cxa_demangle
#endif

using namespace __tsan;

static int DBG_INIT = 0;

static pthread_mutex_t debug_info_lock = PTHREAD_MUTEX_INITIALIZER;

class DbgInfoLock {
 public:
  DbgInfoLock() {
    __real_pthread_mutex_lock(&debug_info_lock);
  }
  ~DbgInfoLock() {
    __real_pthread_mutex_unlock(&debug_info_lock);
  }
};

struct LLVMDebugInfo {
  LLVMDebugInfo()
    : pc(0), line(0) { }
  pc_t pc;
  string symbol;
  string demangled_symbol;
  string file;
  string path;
  string fullpath;  // = path + "/" + file
  uintptr_t line;
};

struct PcInfo {
  uintptr_t pc;
  uintptr_t symbol;
  uintptr_t path;
  uintptr_t file;
  uintptr_t line;
};

static map<pc_t, LLVMDebugInfo> *debug_info = NULL;
// end of section : start of section
static map<uintptr_t, uintptr_t> *data_sections = NULL;

void atexit_callback();

void ReadDbgInfoFromSection(char* start, char* end) {
  if (G_flags->verbosity >= 1) {
    Printf("ReadDbgInfoFromSection: %p to %p\n", start, end);
  }
  DCHECK(IN_RTL); // operator new and map are used below.
  static const int kDebugInfoMagicNumber = 0xdb914f0;
  char *p = start;
  debug_info = new map<pc_t, LLVMDebugInfo>;
  while (p < end) {
    while ((p < end) && (*((int*)p) != kDebugInfoMagicNumber)) p++;
    if (p >= end) break;
    uintptr_t *head = (uintptr_t*)p;
    uintptr_t paths_size = head[1];
    uintptr_t files_size = head[2];
    uintptr_t symbols_size = head[3];
    uintptr_t pcs_size = head[4];
    p += 5 * sizeof(uintptr_t);

    char *paths_raw = p;
    map<uintptr_t, string> paths;
    uintptr_t paths_count = 0;
    char *pstart = paths_raw;
    for (size_t i = 0; i < paths_size; i++) {
      if (!pstart) pstart = paths_raw + i;
      if (paths_raw[i] == '\0') {
        paths[paths_count++] = string(pstart);
        DDPrintf("path: %s\n", paths[paths_count-1].c_str());
        pstart = NULL;
      }
    }

    char *files_raw = paths_raw + paths_size;
    map<uintptr_t, string> files;
    uintptr_t files_count = 0;
    char *fstart = files_raw;
    for (size_t i = 0; i < files_size; i++) {
      if (!fstart) fstart = files_raw + i;
      if (files_raw[i] == '\0') {
        files[files_count++] = string(fstart);
        DDPrintf("file: %s\n", files[files_count-1].c_str());
        fstart = NULL;
      }
    }

    map<uintptr_t, string> symbols;
    uintptr_t symbols_count = 0;
    char *symbols_raw = files_raw + files_size;
    char *sstart = symbols_raw;
    for (size_t i = 0; i < symbols_size; i++) {
      if (!sstart) sstart = symbols_raw + i;
      if (symbols_raw[i] == '\0') {
        symbols[symbols_count++] = string(sstart);
        DDPrintf("symbol: %s\n", symbols[symbols_count-1].c_str());
        sstart = NULL;
      }
    }
    size_t pad = (uintptr_t)(symbols_raw + symbols_size) % sizeof(uintptr_t);
    if (pad) pad = sizeof(uintptr_t) - pad;
    PcInfo *pcs = (PcInfo*)(symbols_raw + symbols_size + pad);
    for (size_t i = 0; i < pcs_size; i++) {
      DDPrintf("pc: %p, sym: %s, file: %s, path: %s, line: %d\n",
             pcs[i].pc, symbols[pcs[i].symbol].c_str(),
             files[pcs[i].file].c_str(), paths[pcs[i].path].c_str(),
             pcs[i].line);
      // If we've already seen this pc, check that it is inside the same
      // function, but do not update the debug info (it may be different).
      // TODO(glider): generate more correct debug info.
      if ((*debug_info).find(pcs[i].pc) != (*debug_info).end()) {
        if ((*debug_info)[pcs[i].pc].symbol != symbols[pcs[i].symbol]) {
          DPrintf("ERROR: conflicting symbols for pc %p:\n  %s\n  %s\n",
                 pcs[i].pc,
                 (*debug_info)[pcs[i].pc].symbol.c_str(),
                 symbols[pcs[i].symbol].c_str());
///          CHECK(false);
        }
      } else {
        (*debug_info)[pcs[i].pc].pc = pcs[i].pc;
        (*debug_info)[pcs[i].pc].symbol = symbols[pcs[i].symbol];
#if defined(__GNUC__)
        char *demangled = NULL;
        int status;
        demangled = __cxxabiv1::__cxa_demangle(symbols[pcs[i].symbol].c_str(),
                                               0, 0, &status);
        if (demangled) {
          (*debug_info)[pcs[i].pc].demangled_symbol = demangled;
          __real_free(demangled);
        } else {
          (*debug_info)[pcs[i].pc].demangled_symbol = symbols[pcs[i].symbol];
        }
#else
        (*debug_info)[pcs[i].pc].demangled_symbol = symbols[pcs[i].symbol];
#endif
        (*debug_info)[pcs[i].pc].file = files[pcs[i].file];
        (*debug_info)[pcs[i].pc].path = paths[pcs[i].path];
        // TODO(glider): move the path-related logic to the compiler.
        if ((files[pcs[i].file] != "")  && (paths[pcs[i].path] != "")) {
          if (paths[pcs[i].path][paths[pcs[i].path].size() - 1] != '/') {
            (*debug_info)[pcs[i].pc].fullpath =
                paths[pcs[i].path] + "/" + files[pcs[i].file];
          } else {
            (*debug_info)[pcs[i].pc].fullpath =
                paths[pcs[i].path] + files[pcs[i].file];
          }
        } else {
          (*debug_info)[pcs[i].pc].fullpath = files[pcs[i].file];
        }
        (*debug_info)[pcs[i].pc].line = pcs[i].line;
      }
    }
  }
}

void AddOneWrapperDbgInfo(pc_t pc, const char *symbol) {
  if (debug_info == 0)
    return;
  char const* prefix = "__real_";
  size_t const prefix_len = strlen(prefix);
  if (strncmp(symbol, prefix, prefix_len) == 0)
    symbol = symbol + prefix_len;
  (*debug_info)[pc].pc = pc;
  (*debug_info)[pc].symbol = symbol;
  (*debug_info)[pc].demangled_symbol = symbol;
  (*debug_info)[pc].fullpath = __FILE__;
  (*debug_info)[pc].file = __FILE__;
  (*debug_info)[pc].path = "";
  // TODO(glider): we need exact line numbers.
  (*debug_info)[pc].line = 0;
}

#define WRAPPER_DBG_INFO(fun) AddOneWrapperDbgInfo((pc_t)fun, #fun)

void AddWrappersDbgInfo() {
  WRAPPER_DBG_INFO(__real_pthread_create);
  WRAPPER_DBG_INFO(__real_pthread_join);
  WRAPPER_DBG_INFO(__real_pthread_mutex_init);
  WRAPPER_DBG_INFO(__real_pthread_mutex_lock);
  WRAPPER_DBG_INFO(__real_pthread_mutex_trylock);
  WRAPPER_DBG_INFO(__real_pthread_mutex_unlock);
  WRAPPER_DBG_INFO(__real_pthread_mutex_destroy);

  WRAPPER_DBG_INFO(__real_pthread_rwlock_init);
  WRAPPER_DBG_INFO(__real_pthread_rwlock_destroy);
  WRAPPER_DBG_INFO(__real_pthread_rwlock_trywrlock);
  WRAPPER_DBG_INFO(__real_pthread_rwlock_wrlock);
  WRAPPER_DBG_INFO(__real_pthread_rwlock_tryrdlock);
  WRAPPER_DBG_INFO(__real_pthread_rwlock_rdlock);
  WRAPPER_DBG_INFO(__real_pthread_rwlock_unlock);

  WRAPPER_DBG_INFO(__real_pthread_spin_init);
  WRAPPER_DBG_INFO(__real_pthread_spin_destroy);
  WRAPPER_DBG_INFO(__real_pthread_spin_lock);
  WRAPPER_DBG_INFO(__real_pthread_spin_trylock);
  WRAPPER_DBG_INFO(__real_pthread_spin_unlock);

  WRAPPER_DBG_INFO(__real_pthread_cond_signal);
  WRAPPER_DBG_INFO(__real_pthread_cond_wait);
  WRAPPER_DBG_INFO(__real_pthread_cond_timedwait);

  WRAPPER_DBG_INFO(__real_pthread_key_create);

  WRAPPER_DBG_INFO(__real_sem_open);
  WRAPPER_DBG_INFO(__real_sem_wait);
  WRAPPER_DBG_INFO(__real_sem_trywait);
  WRAPPER_DBG_INFO(__real_sem_post);
  WRAPPER_DBG_INFO(__real_sem_getvalue);

  WRAPPER_DBG_INFO(__real_usleep);
  WRAPPER_DBG_INFO(__real_nanosleep);
  WRAPPER_DBG_INFO(__real_sleep);
  WRAPPER_DBG_INFO(__real_clock_nanosleep);
  WRAPPER_DBG_INFO(__real_sched_yield);
  WRAPPER_DBG_INFO(__real_pthread_yield);

  WRAPPER_DBG_INFO(__real___cxa_guard_acquire);
  WRAPPER_DBG_INFO(__real___cxa_guard_release);

  WRAPPER_DBG_INFO(__real_atexit);
  WRAPPER_DBG_INFO(__real_exit);

  WRAPPER_DBG_INFO(__real_strlen);
  WRAPPER_DBG_INFO(__real_strcmp);
  WRAPPER_DBG_INFO(real_memchr);
  WRAPPER_DBG_INFO(__real_memcpy);
  WRAPPER_DBG_INFO(__real_memmove);
  WRAPPER_DBG_INFO(__real_strchr);
  WRAPPER_DBG_INFO(__real_strrchr);

  WRAPPER_DBG_INFO(__real_mmap);
  WRAPPER_DBG_INFO(__real_munmap);
  WRAPPER_DBG_INFO(__real_calloc);
  WRAPPER_DBG_INFO(__real_malloc);
  WRAPPER_DBG_INFO(__real_realloc);
  WRAPPER_DBG_INFO(__real_free);
  WRAPPER_DBG_INFO(__real_posix_memalign);

  WRAPPER_DBG_INFO(malloc);
  WRAPPER_DBG_INFO(free);
  WRAPPER_DBG_INFO(realloc);
  WRAPPER_DBG_INFO(calloc);

  WRAPPER_DBG_INFO(__real_read);
  WRAPPER_DBG_INFO(__real_write);

  WRAPPER_DBG_INFO(__real_epoll_ctl);
  WRAPPER_DBG_INFO(__real_epoll_wait);

  WRAPPER_DBG_INFO(__real_pthread_once);
  WRAPPER_DBG_INFO(__real_pthread_barrier_init);
  WRAPPER_DBG_INFO(__real_pthread_barrier_wait);

  WRAPPER_DBG_INFO(__real_sigaction);

#ifdef TSAN_RTL_X86
  WRAPPER_DBG_INFO(__real__Znwj);
  WRAPPER_DBG_INFO(__real__ZnwjRKSt9nothrow_t);
  WRAPPER_DBG_INFO(__real__Znaj);
  WRAPPER_DBG_INFO(__real__ZnajRKSt9nothrow_t);
#endif  // TSAN_RTL_X86
#ifdef TSAN_RTL_X64
  WRAPPER_DBG_INFO(__real__Znwm);
  WRAPPER_DBG_INFO(__real__ZnwmRKSt9nothrow_t);
  WRAPPER_DBG_INFO(__real__Znam);
  WRAPPER_DBG_INFO(__real__ZnamRKSt9nothrow_t);
#endif  // TSAN_RTL_X64
  WRAPPER_DBG_INFO(__real__ZdlPv);
  WRAPPER_DBG_INFO(__real__ZdlPvRKSt9nothrow_t);
  WRAPPER_DBG_INFO(__real__ZdaPv);
  WRAPPER_DBG_INFO(__real__ZdaPvRKSt9nothrow_t);

  WRAPPER_DBG_INFO(atexit_callback);
  WRAPPER_DBG_INFO(__real_strcpy);
  WRAPPER_DBG_INFO(__real_strncmp);

  WRAPPER_DBG_INFO(rtl_memmove);
  WRAPPER_DBG_INFO(rtl_memcpy);
}

string GetSelfFilename() {
  const int kBufSize = 1000;
  char fname[kBufSize];
  memset(fname, '\0', sizeof(fname));
  int fsize = readlink("/proc/self/exe", fname, kBufSize);
  CHECK(fsize < kBufSize);
  return fname;
}

void ReadElf() {
  string fname = GetSelfFilename();
  int fd = open(fname.c_str(), 0);
  if (fd == -1) {
    perror("open");
    Printf("Could not open %s. Debug info will be unavailable.\n",
           fname.c_str());
    return;
  }
  struct stat st;
  fstat(fd, &st);
  DDPrintf("Reading debug info from %s (%d bytes)\n", fname, st.st_size);
  char* map = (char*)__real_mmap(NULL, st.st_size,
                                 PROT_READ, MAP_PRIVATE, fd, 0);
  if (map == MAP_FAILED) {
    perror("mmap");
    Printf("Could not map %s. Debug info will be unavailable.\n", fname.c_str());
    return;
  }

#ifdef TSAN_RTL_X86
  typedef Elf32_Ehdr Elf_Ehdr;
  typedef Elf32_Shdr Elf_Shdr;
  typedef Elf32_Off Elf_Off;
  typedef Elf32_Word Elf_Word;
  typedef Elf32_Addr Elf_Addr;
#else
  typedef Elf64_Ehdr Elf_Ehdr;
  typedef Elf64_Shdr Elf_Shdr;
  typedef Elf64_Off Elf_Off;
  typedef Elf64_Word Elf_Word;
  typedef Elf64_Addr Elf_Addr;
#endif
  Elf_Ehdr* ehdr = (Elf_Ehdr*)map;
  Elf_Shdr* shdrs = (Elf_Shdr*)(map + ehdr->e_shoff);
  char *hdr_strings = map + shdrs[ehdr->e_shstrndx].sh_offset;
  int shnum = ehdr->e_shnum;

  char* debug_info_section = NULL;
  size_t debug_info_size = 0;

  ENTER_RTL();
  for (int i = 0; i < shnum; ++i) {
    Elf_Shdr* shdr = shdrs + i;
    Elf_Off off = shdr->sh_offset;
    Elf_Word name = shdr->sh_name;
    Elf_Word size = shdr->sh_size;
    Elf_Word flags = shdr->sh_flags;
    Elf_Addr vma = shdr->sh_addr;
    DDPrintf("Section name: %d, %s\n", name, hdr_strings + name);
    if (strcmp(hdr_strings + name, "tsan_rtl_debug_info") == 0) {
      debug_info_section = map + off;
      debug_info_size = size;
      continue;
    }
    if (flags & SHF_ALLOC) {
      // TODO(glider): does any other section contain globals?
      if ((strcmp(hdr_strings + name, ".bss") == 0) ||
          (strcmp(hdr_strings + name, ".rodata") == 0) ||
          (strcmp(hdr_strings + name, ".data") == 0)) {
        (*data_sections)[(uintptr_t)(vma + size)] = (uintptr_t)vma;
        DDPrintf("section: %s, data_sections[%p] = %p\n",
                 hdr_strings + name, (uintptr_t)(vma + size), (uintptr_t)vma);
      }
    }
  }

  if (debug_info_section) {
    // Parse the debug info section.
    ReadDbgInfoFromSection(debug_info_section,
                           debug_info_section + debug_info_size);
  }
  LEAVE_RTL();
  // Finalize.
  __real_munmap(map, st.st_size);
  close(fd);
}

void DumpDataSections() {
  map<uintptr_t, uintptr_t>::iterator iter = data_sections->begin();
  Printf("Data sections:\n");
  while(iter != data_sections->end()) {
    Printf("[%p, %p]\n", iter->second, iter->first);
    ++iter;
  }
}

bool IsAddrFromDataSections(uintptr_t addr) {
  map<uintptr_t, uintptr_t>::iterator iter = data_sections->lower_bound(addr);
  if (iter == data_sections->end()) return false;  // lookup failed.
  if ((iter->first >= addr) && (iter->second <= addr)) {
    return true;
  } else {
    return false;
  }
}

void __tsan::SymbolizeInit() {
  CHECK(DBG_INIT == 0);
  data_sections = new map<uintptr_t, uintptr_t>;
  ReadElf();
  AddWrappersDbgInfo();
  DBG_INIT = 1;
}

bool __tsan::SymbolizeData(uintptr_t addr, string *symbol, uintptr_t *offset) {
  (void)addr;
  (void)symbol;
  (void)offset;
  return false;
}

bool __tsan::SymbolizeCode(uintptr_t pc, bool demangle, string *module,
                           string *symbol, string *file, int *line) {
  DbgInfoLock scoped;
  module->clear();
  symbol->clear();
  file->clear();
  line[0] = 0;
  if (debug_info) {
    if (debug_info->find(pc) != debug_info->end()) {
      module = NULL;
      if (demangle) {
        *symbol = (*debug_info)[pc].demangled_symbol;
      } else {
        *symbol = (*debug_info)[pc].symbol;
      }
      *file = ((*debug_info)[pc].fullpath);
      *line = ((*debug_info)[pc].line);
    }
  }
  return true;
}

