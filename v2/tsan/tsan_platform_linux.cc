//===-- tsan_platform_linux.cc ----------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
// Linux-specific code.
//===----------------------------------------------------------------------===//

#include "tsan_platform.h"
#include "tsan_rtl.h"

#include <asm/prctl.h>
#include <elf.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>

extern "C" int arch_prctl(int code, __tsan::uptr *addr);

namespace __tsan {

static uptr g_tls_size;

ScopedInRtl::ScopedInRtl()
    : thr_(cur_thread()) {
  in_rtl_ = thr_->in_rtl;
  thr_->in_rtl++;
  errno_ = errno;
}

ScopedInRtl::~ScopedInRtl() {
  thr_->in_rtl--;
  errno = errno_;
  CHECK_EQ(in_rtl_, thr_->in_rtl);
}

void Printf(const char *format, ...) {
  ScopedInRtl in_rtl;
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

void Report(const char *format, ...) {
  ScopedInRtl in_rtl;
  va_list args;
  va_start(args, format);
  vfprintf(stderr, format, args);
  va_end(args);
}

void Die() {
  _exit(1);
}

static void *my_mmap(void *addr, size_t length, int prot, int flags,
                    int fd, u64 offset) {
  ScopedInRtl in_rtl;
# if __WORDSIZE == 64
  return (void *)syscall(__NR_mmap, addr, length, prot, flags, fd, offset);
# else
  return (void *)syscall(__NR_mmap2, addr, length, prot, flags, fd, offset);
# endif
}

static int my_munmap(void *addr, size_t length) {
  ScopedInRtl in_rtl;
  return syscall(__NR_munmap, addr, length);
}

void *virtual_alloc(uptr size) {
  void *p = my_mmap(0, size, PROT_READ|PROT_WRITE,
      MAP_PRIVATE | MAP_ANON, -1, 0);
  if (p == MAP_FAILED) {
    Report("FATAL: ThreadSanitizer can not allocate %lu MB\n", size<<20);
    Die();
  }
  return p;
}

void virtual_free(void *p, uptr size) {
  ScopedInRtl in_rtl;
  if (my_munmap(p, size)) {
    Report("FATAL: ThreadSanitizer munmap failed\n");
    Die();
  }
}

void sched_yield() {
  ScopedInRtl in_rtl;
  syscall(__NR_sched_yield);
}

static void ProtectRange(uptr beg, uptr end) {
  ScopedInRtl in_rtl;
  if (beg != (uptr)my_mmap((void*)(beg), end - beg,
      PROT_NONE,
      MAP_PRIVATE | MAP_ANON | MAP_FIXED | MAP_NORESERVE,
      -1, 0)) {
    Report("FATAL: ThreadSanitizer can not protect [%p,%p]\n", beg, end);
    Report("FATAL: Make sure you are not using unlimited stack\n");
    Die();
  }
}

void InitializeShadowMemory() {
  const uptr kClosedLowBeg  = 0x200000;
  const uptr kClosedLowEnd  = kLinuxShadowBeg - 1;
  const uptr kClosedHighBeg = kLinuxShadowEnd + 1;
  const uptr kClosedHighEnd = kLinuxAppMemBeg - 1;
  uptr shadow = (uptr)my_mmap((void*)kLinuxShadowBeg,
      kLinuxShadowEnd - kLinuxShadowBeg,
      PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANON | MAP_FIXED | MAP_NORESERVE,
      0, 0);
  if (shadow != kLinuxShadowBeg) {
    Report("FATAL: ThreadSanitizer can not mmap the shadow memory\n");
    Report("FATAL: Make shoure to compile with -fPIE and to link with -pie.\n");
    Die();
  }
  ProtectRange(kClosedLowBeg, kClosedLowEnd);
  ProtectRange(kClosedHighBeg, kClosedHighEnd);
  DPrintf("kClosedLow   %p-%p (%p)\n",
      kClosedLowBeg, kClosedLowEnd, kClosedLowEnd - kClosedLowBeg);
  DPrintf("kLinuxShadow %p-%p (%p)\n",
      kLinuxShadowBeg, kLinuxShadowEnd, kLinuxShadowEnd - kLinuxShadowBeg);
  DPrintf("kClosedHigh  %p-%p (%p)\n",
      kClosedHighBeg, kClosedHighEnd, kClosedHighEnd - kClosedHighBeg);
  DPrintf("kLinuxAppMem %p-%p (%p)\n",
      kLinuxAppMemBeg, kLinuxAppMemEnd, kLinuxAppMemEnd - kLinuxAppMemBeg);
  DPrintf("stack        %p\n", &shadow);
  DPrintf("shadow:      %p\n", shadow);
}

static void CheckPIE() {
  // Ensure that the binary is indeed compiled with -pie.
  int fmaps = open("/proc/self/maps", O_RDONLY);
  if (fmaps != -1) {
    char buf[20];
    if (read(fmaps, buf, sizeof(buf)) == sizeof(buf)) {
      buf[sizeof(buf) - 1] = 0;
      u64 addr = strtoll(buf, 0, 16);
      if ((u64)addr < kLinuxAppMemBeg) {
        Report("FATAL: ThreadSanitizer can not mmap the shadow memory ("
               "something is mapped at 0x%p)\n", addr);
        Report("FATAL: Make shoure to compile with -fPIE"
               " and to link with -pie.\n");
        Die();
      }
    }
    close(fmaps);
  }
}

static int GetTlsSize() {
  // As per csu/libc-tls.c, static TLS block has some surplus bytes beyond the
  // size of .tdata and .tbss.
  int tls_size = 2048;

  int fd = open("/proc/self/exe", 0);
  if (fd == -1) {
    Printf("FATAL: ThreadSanitizer failed to open /proc/self/exe\n");
    Die();
  }
  struct stat st;
  fstat(fd, &st);
  char* map = (char*)my_mmap(0, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (map == MAP_FAILED) {
    Printf("FATAL: ThreadSanitizer failed to mmap /proc/self/exe\n");
    Die();
  }

#ifdef __LP64__
  typedef Elf64_Ehdr Elf_Ehdr;
  typedef Elf64_Shdr Elf_Shdr;
  typedef Elf64_Off Elf_Off;
  typedef Elf64_Word Elf_Word;
  typedef Elf64_Addr Elf_Addr;
#else
  typedef Elf32_Ehdr Elf_Ehdr;
  typedef Elf32_Shdr Elf_Shdr;
  typedef Elf32_Off Elf_Off;
  typedef Elf32_Word Elf_Word;
  typedef Elf32_Addr Elf_Addr;
#endif
  Elf_Ehdr* ehdr = (Elf_Ehdr*)map;
  Elf_Shdr* shdrs = (Elf_Shdr*)(map + ehdr->e_shoff);
  char *hdr_strings = map + shdrs[ehdr->e_shstrndx].sh_offset;
  int shnum = ehdr->e_shnum;

  for (int i = 0; i < shnum; ++i) {
    Elf_Shdr* shdr = shdrs + i;
    Elf_Word name = shdr->sh_name;
    Elf_Word size = shdr->sh_size;
    Elf_Word flags = shdr->sh_flags;
    if (flags & SHF_TLS) {
      if ((strcmp(hdr_strings + name, ".tbss") == 0) ||
          (strcmp(hdr_strings + name, ".tdata") == 0)) {
        tls_size += size;
      }
    }
  }

  my_munmap(map, st.st_size);
  close(fd);
  return tls_size;
}

void InitializePlatform() {
  void *p = 0;
  if (sizeof(p) == 8) {
    // Disable core dumps, dumping of 16TB usually takes a bit long.
    // The following magic is to prevent clang from replacing it with memset.
    volatile rlimit lim;
    lim.rlim_cur = 0;
    lim.rlim_max = 0;
    setrlimit(RLIMIT_CORE, (rlimit*)&lim);
  }

  CheckPIE();
  g_tls_size = (uptr)GetTlsSize();
}

void GetThreadStackAndTls(uptr *stk_addr, uptr *stk_size,
                          uptr *tls_addr, uptr *tls_size) {
  *stk_addr = 0;
  *stk_size = 0;
  pthread_attr_t attr;
  if (pthread_getattr_np(pthread_self(), &attr) == 0) {
    pthread_attr_getstack(&attr, (void**)stk_addr, (size_t*)stk_size);
    pthread_attr_destroy(&attr);
  }
  arch_prctl(ARCH_GET_FS, tls_addr);
  *tls_addr -= g_tls_size;
  *tls_size = g_tls_size;
}

}  // namespace __tsan
