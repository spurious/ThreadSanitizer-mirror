/* Relite
 * Copyright (c) 2011, Google Inc.
 * All rights reserved.
 * Author: Dmitry Vyukov (dvyukov@google.com)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
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

#include "relite_report.h"
#include "relite_atomic.h"
#include "relite_dbg.h"
#include "relite_rt.h"
#include "relite_rt_int.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <execinfo.h>
#include <malloc.h>
#include <assert.h>
#include <limits.h>
#include <sched.h>

#include "bfd.h"
#include "demangle.h"
#include "libiberty.h"

#define RELITE_PRINT_STACK


typedef struct libtrace_data_t {
  atomic_uint32_t                           mtx;
  bfd_boolean                               unwind_inlines;
  bfd_boolean                               base_names;
  asymbol**                                 syms;
  bfd*                                      abfd;
  asection*                                 section;
} libtrace_data_t;


typedef struct sym_info_t {
  bfd_vma pc;
  const char* filename;
  const char* functionname;
  unsigned int line;
  bfd_boolean found;
} sym_info_t;


static libtrace_data_t g_libtrace_data = {
  .mtx                                      = {0},
  .unwind_inlines                           = TRUE,
  .base_names                               = TRUE,
  .syms                                     = NULL,
  .abfd                                     = NULL,
  .section                                  = NULL,
};


typedef int(*report_hook_f)(relite_report_t const*);
static report_hook_f volatile g_report_hook;

void                    relite_report_hook  (report_hook_f hook) {
  g_report_hook = hook;
}


void                    relite_report_init  () {
  FILE* cmdline = fopen("/proc/self/cmdline", "rb");
  if (cmdline == 0)
    relite_fatal("failed to open /proc/self/cmdline (%d)", errno);
  char buf [PATH_MAX + 1];
  if (fread(buf, 1, sizeof(buf)/sizeof(buf[0]) - 1, cmdline) <= 0)
    relite_fatal("failed to read /proc/self/cmdline (%d)", errno);
  fclose(cmdline);

  bfd_init();
  bfd* abfd = bfd_openr(buf, 0);
  g_libtrace_data.abfd = abfd;
  if (abfd == NULL)
    relite_fatal("bfd_openr() failed");
  if (bfd_check_format(abfd, bfd_archive))
    relite_fatal("bfd_check_format() failed");
  char** matching = NULL;
  if (!bfd_check_format_matches(abfd, bfd_object, &matching))
    relite_fatal("bfd_check_format_matches() failed");
  if ((bfd_get_file_flags(abfd) & HAS_SYMS) == 0)
    relite_fatal("bfd_get_file_flags() failed");

  asymbol** syms = 0;
  unsigned int size;
  long symcount = bfd_read_minisymbols(abfd, 0, (void*)&syms, &size);
  if (symcount == 0)
    symcount = bfd_read_minisymbols(abfd, 1, (void*)&syms, &size);
  if (symcount < 0)
    relite_fatal("bfd_read_minisymbols() failed");
  g_libtrace_data.syms = syms;
}


static void             find_address_in_section (bfd* abfd,
                                                 asection* section,
                                                 void* data) {
  bfd_vma vma;
  bfd_size_type size;
  sym_info_t* psi = (sym_info_t*)data;

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

  psi->found = bfd_find_nearest_line(abfd, section,
                 g_libtrace_data.syms, psi->pc - vma,
                 &psi->filename, &psi->functionname,
                 &psi->line);
}


static void             translate_addresses (bfd* abfd,
                                             void* xaddr,
                                             char* buf_func,
                                             size_t buf_func_len,
                                             char* buf_file,
                                             size_t buf_file_len) {
  char addr [(CHAR_BIT/4) * (sizeof(void*)) + 2] = {0};
  sprintf(addr, "%p", xaddr);
  sym_info_t si = {0};
  si.pc = bfd_scan_vma (addr, NULL, 16);
  si.found = FALSE;
  bfd_map_over_sections(abfd, find_address_in_section, &si);

  if (si.found == 0) {
    if (buf_func != 0 && buf_func_len != 0)
      snprintf(buf_func, buf_func_len, "%s", "??");
    if (buf_file != 0 && buf_file_len != 0)
      snprintf(buf_file, buf_file_len, "%s", "??:??");
  } else {
    do {
      char* alloc = 0;
      const char* name = si.functionname;
      if (name == 0 || *name == '\0') {
        name = "??";
      } else {
        alloc = bfd_demangle(abfd, name, DMGL_ANSI | DMGL_PARAMS);
        if (alloc != NULL)
          name = alloc;
      }
      if (buf_func != NULL) {
        int name_len = strlen(name);
        int has_paren = name_len != 0 && name[name_len - 1] == ')';
        snprintf(buf_func, buf_func_len, (has_paren ? "%s" : "%s()"), name);
      }
      if (alloc != 0)
        free(alloc);

      if (g_libtrace_data.base_names && si.filename != 0) {
        char* h = strrchr(si.filename, '/');
        if (h != 0)
          si.filename = h + 1;
      }

      if (buf_file != NULL)
        snprintf(buf_file, buf_file_len, "%s:%u",
                si.filename ? si.filename : "??", si.line);
      if (!g_libtrace_data.unwind_inlines)
        si.found = FALSE;
      else
        si.found = bfd_find_inliner_info(abfd,
                                         &si.filename,
                                         &si.functionname,
                                         &si.line);
    } while (si.found);
  }
}


void                    relite_report       (addr_t addr,
                                             state_t state,
                                             int is_load) {
  relite_report_t report = {};
  report.addr = addr;

  uint64_t const sz_code = (state & STATE_SIZE_MASK) >> STATE_SIZE_SHIFT;
  report.size = 8;
  if (sz_code == 1)
    report.size = 4;
  else if (sz_code == 2)
    report.size = 2;
  else if (sz_code == 3)
    report.size = 1;

  int const is_load2 = (state & STATE_LOAD_MASK) != 0;
  timestamp_t const ts = (state & STATE_TIMESTAMP_MASK);
  if (ts == STATE_UNITIALIZED)
    report.type = relite_report_unitialized;
  else if (ts == STATE_MINE_ZONE)
    report.type = relite_report_out_of_bounds;
  else if (ts == STATE_FREED)
    report.type = relite_report_use_after_free;
  else if (is_load == 0 && is_load2 == 0)
    report.type = relite_report_race_store;
  else
    report.type = relite_report_race_load;

  //handle_region_store(&report, &report + sizeof(report));
  report_hook_f hook = g_report_hook;
  if (hook && hook(&report))
    return;

  unsigned my_tid = (unsigned)pthread_self();
  if (my_tid == 0)
    my_tid = 1;
  if (atomic_uint32_load(&g_libtrace_data.mtx, memory_order_relaxed) == my_tid)
    return;
  while (atomic_uint32_exchange
      (&g_libtrace_data.mtx, my_tid, memory_order_acquire) != 0)
    sched_yield();

  FILE* out = stdout;
#ifdef RELITE_PRINT_STACK
  fprintf(out, "\n--------------------------------\n");
#endif
  fprintf(out, "%s on %p (%u bytes)\n",
          relite_report_str(report.type),
          addr,
          report.size);

#ifdef RELITE_PRINT_STACK
  void* stacktrace [64];
  int stacktrace_size = backtrace(
      stacktrace, sizeof(stacktrace)/sizeof(stacktrace[0]));
  int i;
  int pos = 0;
  for (i = 0; i != stacktrace_size; i += 1) {
    char buf_func [PATH_MAX + 1];
    char buf_file [PATH_MAX + 1];
    translate_addresses(g_libtrace_data.abfd,
                        stacktrace[i],
                        buf_func, sizeof(buf_func)/sizeof(buf_func[0]) - 1,
                        buf_file, sizeof(buf_file)/sizeof(buf_file[0]) - 1);
    if (strcmp(buf_func, "relite_thread_wrapper()") == 0)
      break;
    if (strncmp(buf_func, "relite_", sizeof("relite_") - 1) == 0)
      continue;
    fprintf(out, "  #%d %s, %s\n",
            pos, buf_func, buf_file);
    pos += 1;
    if (strcmp(buf_func, "main()") == 0)
      break;
  }
  fprintf(out, "--------------------------------\n\n");
#endif

  atomic_uint32_store(&g_libtrace_data.mtx, 0, memory_order_release);
}


char const*             relite_report_str   (relite_report_type_e type) {
  switch (type) {
    case relite_report_race_load:     return "store-load race";
    case relite_report_race_store:    return "store-store race";
    case relite_report_use_after_free:return "access to freed memory";
    case relite_report_unitialized:   return "access to unitialized memory";
    case relite_report_out_of_bounds: return "out of bounds access";
  }
  //assert(!"unknown report type");
  return "unknown";
}





