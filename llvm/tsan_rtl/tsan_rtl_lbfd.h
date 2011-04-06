// Copyright 2011 Google Inc. All Rights Reserved.
// Author: glider@google.com (Alexander Potapenko),
//         dvyukov@google.com (Dmitriy Vyukov)

#ifndef TSAN_RTL_LBFD_H_
#define TSAN_RTL_LBFD_H_

#include "tsan_rtl.h"

#include <bfd.h>
#include <unistd.h>

namespace tsan_rtl_lbfd {

bool ReadGlobalsFromImage(PcToStringMap *global_symbols);
bool BfdInit();

string BfdPcToRtnName(pc_t pc, bool demangle);

void BfdPcToStrings(pc_t pc, bool demangle,
                    string *img_name, string *rtn_name,
                    string *file_name, int *line_no);

}  // namespace tsan_rtl_lbfd

#endif  // TSAN_RTL_LBFD_H_
