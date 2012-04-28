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

#include "tsan_suppressions.h"
#include "tsan_rtl.h"
#include "tsan_flags.h"
#include "tsan_mman.h"

#include <linux/limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

namespace __tsan {

static Suppression *g_suppressions;

static char *ReadFile(const char *filename) {
  if (filename == 0 || filename[0] == 0)
    return 0;
  InternalScopedBuf<char> tmp(PATH_MAX);
  if (filename[0] == '/')
    Snprintf(tmp, tmp.Size(), "%s", filename);
  else
    Snprintf(tmp, tmp.Size(), "%s/%s", getenv("PWD"), filename);
  int fd = open(tmp, O_RDONLY);
  if (fd == -1) {
    Printf("ThreadSanitizer: failed to open suppressions file '%s'\n",
        tmp.Ptr());
    Die();
  }
  struct stat st;
  if (fstat(fd, &st)) {
    Printf("ThreadSanitizer: failed to stat suppressions file '%s'\n",
        tmp.Ptr());
    Die();
  }
  if (st.st_size == 0) {
    close(fd);
    return 0;
  }
  char *buf = (char*)internal_alloc(MBlockSuppression, st.st_size + 1);
  if (st.st_size != read(fd, buf, st.st_size)) {
    Printf("ThreadSanitizer: failed to read suppressions file '%s'\n",
        tmp.Ptr());
    Die();
  }
  close(fd);
  buf[st.st_size] = 0;
  return buf;
}

bool SuppressionMatch(char *templ, const char *str) {
  char *tpos;
  const char *spos;
  while (templ && templ[0]) {
    if (templ[0] == '*') {
      templ++;
      continue;
    }
    if (str[0] == 0)
      return false;
    tpos = (char*)internal_strchr(templ, '*');
    if (tpos != 0)
      tpos[0] = 0;
    spos = internal_strstr(str, templ);
    str = spos + internal_strlen(templ);
    templ = tpos;
    if (tpos)
      tpos[0] = '*';
    if (spos == 0)
      return false;
  }
  return true;
}

Suppression *SuppressionParse(const char* supp) {
  Suppression *head = 0;
  const char *line = supp;
  while (line) {
    while (line[0] == ' ' || line[0] == '\t')
      line++;
    const char *end = internal_strchr(line, '\n');
    if (end == 0)
      end = line + internal_strlen(line);
    if (line != end && line[0] != '#') {
      const char *end2 = end;
      while (line != end2 && (end2[-1] == ' ' || end2[-1] == '\t'))
        end2--;
      Suppression *s = (Suppression*)internal_alloc(MBlockSuppression,
          sizeof(Suppression));
      s->next = head;
      head = s;
      s->func = (char*)internal_alloc(MBlockSuppression, end2 - line + 1);
      internal_memcpy(s->func, line, end2 - line);
      s->func[end2 - line] = 0;
    }
    if (end[0] == 0)
      break;
    line = end + 1;
  }
  return head;
}

void SuppressionFree(Suppression *supp) {
  while (supp) {
    Suppression *tmp = supp;
    supp = tmp->next;
    internal_free(tmp->func);
    internal_free(tmp);
  }  
}

void InitializeSuppressions() {
  char *supp = ReadFile(flags()->suppressions);
  g_suppressions = SuppressionParse(supp);
}

void FinalizeSuppressions() {
  SuppressionFree(g_suppressions);
  g_suppressions = 0;
}

bool IsSuppressed(ReportType typ, const ReportStack *stack) {
  if (g_suppressions == 0 || stack == 0 || typ != ReportTypeRace)
    return false;
  for (const ReportStack *frame = stack; frame; frame = frame->next) {
    if (frame->func == 0)
      continue;
    for (Suppression *supp = g_suppressions; supp; supp = supp->next) {
      if (SuppressionMatch(supp->func, frame->func)) {
        DPrintf("ThreadSanitizer: matched suppression '%s'\n", supp->func);
        return true;
      }
    }
  }
  return false;
}
}  // namespace __tsan
