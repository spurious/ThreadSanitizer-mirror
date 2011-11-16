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

// Author: <Dmitry Vyukov> dvyukov@google.com

#include "bfd_symbolizer.h"
#include "thread_sanitizer.h"
#include "tsan_rtl_symbolize.h"

static int UnwindCallback(uintptr_t* stack, int count, uintptr_t pc) {
  int res = bfds_unwind((void**)stack, count, 0);
  if (res <= 0 || res > count)
    return -1;
  for (int i = 1; i < res; i++) {
    if (stack[i] == pc) {
      for (int j = i; j < res; j++) {
        stack[j-i] = stack[j];
      }
      res -= i;
      break;
    }
  }
  return res;
}

void __tsan::SymbolizeInit() {
  ThreadSanitizerSetUnwindCallback(UnwindCallback);
}

bool __tsan::SymbolizeData(void *addr, char *symbol,
                           int symbol_sz, uintptr_t *offset) {
  int soffset = 0;
  if (bfds_symbolize(addr,
                     (bfds_opts_e)(bfds_opt_data | bfds_opt_demangle),
                     symbol, symbol_sz,
                     0, 0, // module
                     0, 0, // source file
                     0,    // source line
                     &soffset)) {
    return false;
  }

  if (offset)
    *offset = soffset;
  return true;
}

bool __tsan::SymbolizeCode(void *pc, bool demangle,
                           char *module, int module_sz,
                           char *symbol, int symbol_sz,
                           char *file, int file_sz,
                           int *line) {
  if (bfds_symbolize((void*)pc,
                     demangle ? bfds_opt_demangle : bfds_opt_none,
                     symbol, symbol_sz,
                     module, module_sz,
                     file, file_sz,
                     line,
                     0)) { // symbol offset
    if (module && module_sz)
      module[0] = 0;
    if (symbol && symbol_sz)
      symbol[0] = 0;
    if (file && file_sz)
      file[0] = 0;
    if (line)
      line[0] = 0;
    return false;
  }

  return true;
}

