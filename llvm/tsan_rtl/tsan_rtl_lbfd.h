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

#ifndef TSAN_RTL_LBFD_H_
#define TSAN_RTL_LBFD_H_

#include "tsan_rtl.h"

#include <bfd.h>
#include <unistd.h>

namespace tsan_rtl_lbfd {

PcToStringMap* ReadGlobalsFromImage();
bool BfdInit();

string BfdPcToRtnName(pc_t pc, bool demangle);

void BfdPcToStrings(pc_t pc, bool demangle,
                    string *img_name, string *rtn_name,
                    string *file_name, int *line_no);

}  // namespace tsan_rtl_lbfd

#endif  // TSAN_RTL_LBFD_H_
