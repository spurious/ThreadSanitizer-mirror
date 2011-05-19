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

// Author: glider@google.com (Alexander Potapenko),
//         dvyukov@google.com (Dmitriy Vyukov)

// The following code uses libbfd and requires the ThreadSanitizer runtime
// library to be linked with -lbfd, thus enforcing the GPL license. Note that
// the rest of runtime library, including ThreadSanitizer itself, is distributed
// under the BSD license.

// Parts of runtime library that interact with libbfd.
// At the moment libbfd is disabled for the 32-bit version of the RTL, because
// Ubuntu (our main target) doesn't have a package with the 32-bit libbfd.

#include "tsan_rtl_lbfd.h"
#include <bfd.h>
#include <unistd.h>
#include <cxxabi.h>
#include <memory>

namespace demangle { // name clash with 'basename' from <string.h>
# include <demangle.h>
}

namespace tsan_rtl_lbfd {

struct BfdData {
  bfd*                          abfd;
  asymbol**                     syms;
} * bfd_data;

struct BfdSymbol {
  bfd_vma                       pc;
  const char*                   filename;
  const char*                   functionname;
  unsigned int                  line;
  bfd_boolean                   found;
};

// TODO(glider): unify all code that operates BFD.
PcToStringMap* ReadGlobalsFromImage(bool(*IsAddrFromDataSections)(uintptr_t)) {
  std::auto_ptr<PcToStringMap> global_symbols (new map<uintptr_t, string>);
  bfd_init();
  bfd* abfd = bfd_openr("/proc/self/exe", 0);
  if (abfd == 0) {
    bfd_perror("bfd_openr('/proc/self/exe') failed");
    return NULL;
  }
  if (bfd_check_format(abfd, bfd_archive)) {
    bfd_perror("bfd_check_format() failed");
    bfd_close(abfd);
    return NULL;
  }
  char** matching = NULL;
  if (!bfd_check_format_matches(abfd, bfd_object, &matching)) {
    bfd_perror("bfd_check_format_matches() failed");
    bfd_close(abfd);
    return 0;
  }
  if ((bfd_get_file_flags(abfd) & HAS_SYMS) == 0) {
    bfd_perror("bfd_get_file_flags() failed");
    bfd_close(abfd);
    return 0;
  }

  asymbol** syms = 0;
  unsigned int size;
  int dyn = 0;
  long symcount = bfd_read_minisymbols(abfd, dyn, (void**)&syms, &size);
  if (symcount == 0) {
    dyn = 1;
    symcount = bfd_read_minisymbols(abfd, dyn, (void**)&syms, &size);
  }
  if (symcount < 0) {
    bfd_perror("bfd_read_minisymbols() failed");
    bfd_close(abfd);
    return 0;
  }

  char * p = (char*)syms;
  for (int i = 0; i < symcount; i++) {
    asymbol *sym = bfd_minisymbol_to_symbol(abfd, dyn, p, NULL);
    uintptr_t addr = bfd_asymbol_value(sym);
    if (sym->flags & BSF_OBJECT) {
      // Some globals, e.g. rtl_debug_info* or _GLOBAL_OFFSET_TABLE_, are not
      // interesting for us, because they don't belong to the list of known data
      // sections.
      if (IsAddrFromDataSections && IsAddrFromDataSections(addr)) {
        (*global_symbols)[addr] = bfd_asymbol_name(sym);
        DDPrintf("name: %s, value: %p\n",
                 bfd_asymbol_name(sym), addr);
      }
    }
    p += size;
  }
  return global_symbols.release();
}

bool BfdInit() {
  if (bfd_data != 0)
    return true;
  bfd_init();
  bfd* abfd = bfd_openr("/proc/self/exe", 0);
  if (abfd == 0) {
    bfd_perror("bfd_openr('/proc/self/exe') failed");
    return false;
  }
  if (bfd_check_format(abfd, bfd_archive)) {
    bfd_perror("bfd_check_format() failed");
    bfd_close(abfd);
    return false;
  }
  char** matching = NULL;
  if (!bfd_check_format_matches(abfd, bfd_object, &matching)) {
    bfd_perror("bfd_check_format_matches() failed");
    bfd_close(abfd);
    return false;
  }
  if ((bfd_get_file_flags(abfd) & HAS_SYMS) == 0) {
    bfd_perror("bfd_get_file_flags() failed");
    bfd_close(abfd);
    return false;
  }

  asymbol** syms = 0;
  unsigned int size;
  long symcount = bfd_read_minisymbols(abfd, 0, (void**)&syms, &size);
  if (symcount == 0)
    symcount = bfd_read_minisymbols(abfd, 1, (void**)&syms, &size);
  if (symcount < 0) {
    bfd_perror("bfd_read_minisymbols() failed");
    bfd_close(abfd);
    return false;
  }

  bfd_data = new BfdData;
  bfd_data->abfd = abfd;
  bfd_data->syms = syms;
  return true;
}

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

  psi->found = bfd_find_nearest_line(abfd, section,
                                     bfd_data->syms, psi->pc - vma,
                                     &psi->filename, &psi->functionname,
                                     &psi->line);
}

static void BfdTranslateAddress(void* xaddr,
                                char* buf_func,
                                size_t buf_func_len,
                                char* buf_file,
                                size_t buf_file_len,
                                int* line,
                                bool demangle) {
  char addr [(CHAR_BIT/4) * (sizeof(void*)) + 2] = {0};
  sprintf(addr, "%p", xaddr);
  BfdSymbol si = {0};
  si.pc = bfd_scan_vma (addr, NULL, 16);
  si.found = FALSE;
  bfd_map_over_sections(bfd_data->abfd, BfdFindAddressCallback, &si);

  if (si.found == 0) {
    if (buf_func != 0 && buf_func_len != 0)
      snprintf(buf_func, buf_func_len, "%s", "??");
    if (buf_file != 0 && buf_file_len != 0)
      snprintf(buf_file, buf_file_len, "%s", "??:??");
    if (line != 0)
      *line = 0;
  } else {
    do {
      char* alloc = 0;
      const char* name = si.functionname;
      if (name == 0 || *name == '\0') {
        name = "??";
      } else {
        if (demangle) {
          int status = 0;
          alloc = abi::__cxa_demangle(name, 0, 0, &status);
        }
        if (alloc != NULL)
          name = alloc;
      }
      if (buf_func != NULL) {
        snprintf(buf_func, buf_func_len, "%s", name);
      }
      if (alloc != 0)
        free(alloc);

      if (buf_file != NULL)
        snprintf(buf_file, buf_file_len, "%s",
                si.filename ? si.filename : "??");
      if (line != 0)
        *line = si.line;
      si.found = bfd_find_inliner_info(bfd_data->abfd,
                                       &si.filename,
                                       &si.functionname,
                                       &si.line);
    } while (si.found);
  }
}

string BfdPcToRtnName(pc_t pc, bool demangle) {
  if (BfdInit() == false)
    return string();
  char buf_func [PATH_MAX + 1];
  BfdTranslateAddress((void*)pc,
                      buf_func, sizeof(buf_func)/sizeof(buf_func[0]) - 1,
                      0, 0, 0, demangle);
  return buf_func;
}

void BfdPcToStrings(pc_t pc, bool demangle,
                    string *img_name, string *rtn_name,
                    string *file_name, int *line_no) {
  if (BfdInit() == false)
    return;
  char buf_func [PATH_MAX + 1];
  char buf_file [PATH_MAX + 1];
  BfdTranslateAddress((void*)pc,
                      buf_func, sizeof(buf_func)/sizeof(buf_func[0]) - 1,
                      buf_file, sizeof(buf_file)/sizeof(buf_file[0]) - 1,
                      line_no, demangle);
  if (rtn_name != 0)
    rtn_name->assign(buf_func);
  if (file_name != 0)
    file_name->assign(buf_file);
}

} // namespace tsan_rtl_lbfd






