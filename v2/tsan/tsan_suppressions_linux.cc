//===-- tsan_suppressions_linux.cc ------------------------------*- C++ -*-===//
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
//===----------------------------------------------------------------------===//

#include "suppressions.h"  // from v1
#undef DCHECK
#undef CHECK
#undef CHECK_EQ
#undef CHECK_NE
#undef CHECK_LE
#undef CHECK_LT
#undef CHECK_GE
#undef CHECK_GT
#undef UNLIKELY
#undef ALWAYS_INLINE
#undef NOINLINE
#undef INLINE

#include "tsan_suppressions.h"
#include "tsan_rtl.h"
#include "tsan_flags.h"
#include "tsan_mman.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdarg.h>

namespace __tsan {
void Printf(const char *format, va_list args);
}

void ThreadSanitizerPrintf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  __tsan::Printf(format, args);
  va_end(args);
}

namespace __tsan {

static ThreadSanitizerSuppressions *g_suppressions;

static char *ReadFile(const char *filename) {
  if (filename == 0 || filename[0] == 0)
    return 0;
  int fd = open(filename, O_RDONLY);
  if (fd == -1) {
    Printf("ThreadSanitizer: failed to open suppressions file '%s'\n",
        filename);
    Die();
  }
  struct stat st;
  if (fstat(fd, &st)) {
    Printf("ThreadSanitizer: failed to stat suppressions file '%s'\n",
        filename);
    Die();
  }
  if (st.st_size == 0) {
    close(fd);
    return 0;
  }
  char *buf = (char*)internal_alloc(st.st_size + 1);
  if (st.st_size != read(fd, buf, st.st_size)) {
    Printf("ThreadSanitizer: failed to read suppressions file '%s'\n",
        filename);
    Die();
  }
  close(fd);
  buf[st.st_size] = 0;
  return buf;
}

void InitializeSuppressions() {
  char *supp = ReadFile(flags()->suppressions);
  if (supp == 0)
    return;
  void *mem = internal_alloc(sizeof(ThreadSanitizerSuppressions));
  g_suppressions = new(mem) ThreadSanitizerSuppressions;
  if (g_suppressions->ReadFromString(supp) < 0) {
    Printf("ThreadSanitizer: failed to parse suppressions file: %s:%d\n",
        g_suppressions->GetErrorString().c_str(),
        g_suppressions->GetErrorLineNo());
    Die();
  }
  internal_free(supp);
}

bool IsSuppressed(ReportType typ, const ReportStack *stack) {
  if (g_suppressions == 0 || stack == 0 || typ != ReportTypeRace)
    return false;
  vector<string> function_names_demangled;
  for (const ReportStack *frame = stack; frame; frame = frame->next) {
    if (frame->func == 0)
      continue;
    const char *pos1 = internal_strchr(frame->func, '(');
    const char *pos2 = internal_strchr(frame->func, '<');
    const char *end = 0;
    if (pos1 && pos2)
      end = min(pos1, pos2);
    else if (pos1 || pos2)
      end = max(pos1, pos2);
    else
      end = frame->func + internal_strlen(frame->func);
    function_names_demangled.push_back(string((const char*)frame->func, end));
  }
  vector<string> function_names_mangled(function_names_demangled.size());
  vector<string> object_names;
  string suppression;
  bool res = g_suppressions->StackTraceSuppressed("ThreadSanitizer", "Race",
      function_names_mangled, function_names_demangled, object_names,
      &suppression);
  if (res)
    Printf("Matched suppression '%s'\n", suppression.c_str());
  return res;
}
}  // namespace __tsan
