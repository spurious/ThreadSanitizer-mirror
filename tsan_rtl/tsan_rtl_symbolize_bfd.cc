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

bool __tsan::SymbolizeData(uintptr_t addr, string *symbol, uintptr_t *offset) {
  char symbolbuf [4096];
  int soffset = 0;

  if (bfds_symbolize((void*)addr,
                     (bfds_opts_e)(bfds_opt_data | bfds_opt_demangle),
                     symbolbuf, sizeof(symbolbuf),
                     0, 0, // module
                     0, 0, // source file
                     0,    // source line
                     &soffset)) {
    return false;
  }

  if (symbol)
      symbol->assign(symbolbuf);
  if (offset)
    *offset = soffset;
  return true;
}

bool __tsan::SymbolizeCode(uintptr_t pc, bool demangle, string *module,
                           string *symbol, string *file, int *line) {
  char symbolbuf [4096];
  char modulebuf [4096];
  char filebuf   [4096];

  if (bfds_symbolize((void*)pc,
                     demangle ? bfds_opt_demangle : bfds_opt_none,
                     symbolbuf, sizeof(symbolbuf),
                     modulebuf, sizeof(modulebuf),
                     filebuf, sizeof(filebuf),
                     line,
                     0)) { // symbol offset
    if (module)
      module->clear();
    if (symbol)
      symbol->clear();
    if (file)
      file->clear();
    if (line)
      *line = 0;
    return false;
  }

  if (module)
    module->assign(modulebuf);
  if (symbol)
    symbol->assign(symbolbuf);
  if (file)
    file->assign(filebuf);
  return true;
}

