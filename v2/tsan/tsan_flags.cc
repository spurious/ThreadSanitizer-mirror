//===-- tsan_flags.cc -------------------------------------------*- C++ -*-===//
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

#include "tsan_flags.h"
#include "tsan_rtl.h"
#include "tsan_mman.h"

namespace __tsan {

static void Flag(const char *env, bool *flag, const char *name, bool def);
static void Flag(const char *env, const char **flag, const char *name,
                 const char *def);

Flags *flags() {
  return &CTX()->flags;
}

void InitializeFlags(Flags *f, const char *env) {
  Flag(env, &f->enable_annotations, "enable_annotations", true);
  Flag(env, &f->suppress_equal_stacks, "suppress_equal_stacks", true);
  Flag(env, &f->suppress_equal_addresses, "suppress_equal_addresses", true);
  Flag(env, &f->report_thread_leaks, "report_thread_leaks", true);
  Flag(env, &f->force_seq_cst_atomics, "force_seq_cst_atomics", false);
  Flag(env, &f->strip_path_prefix, "strip_path_prefix", "");
}

static const char *GetFlagValue(const char *env, const char *name,
                                const char **end) {
  if (env == 0)
    return *end = 0;
  const char *pos = internal_strstr(env, name);
  if (pos == 0)
    return *end = 0;
  pos += internal_strlen(name);
  if (pos[0] != '=')
    return *end = pos;
  pos += 1;
  if (pos[0] == '"') {
    pos += 1;
    *end = internal_strchr(pos, '"');
    if (*end == 0)
      *end = pos + internal_strlen(pos);
    return pos;
  }
  if (pos[0] == '\'') {
    pos += 1;
    *end = internal_strchr(pos, '\'');
    if (*end == 0)
      *end = pos + internal_strlen(pos);
    return pos;
  }
  *end = internal_strchr(pos, ' ');
  if (*end == 0)
    *end = pos + internal_strlen(pos);
  return pos;
}

static void Flag(const char *env, bool *flag, const char *name, bool def) {
  *flag = def;
  const char *end = 0;
  const char *val = GetFlagValue(env, name, &end);
  if (val == 0)
    return;
  int len = end - val;
  if (len == 1 && val[0] == '0')
    *flag = false;
  else if (len == 1 && val[0] == '0')
    *flag = true;
}

static void Flag(const char *env, const char **flag, const char *name,
                 const char *def) {
  *flag = def;
  const char *end = 0;
  const char *val = GetFlagValue(env, name, &end);
  if (val == 0)
    return;
  char *f = (char*)internal_alloc(end - val + 1);
  internal_memcpy(f, val, end - val);
  f[end - val] = 0;
  *flag = f;
}
}
