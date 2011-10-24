#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>

#include <stdio.h>
#include "string"

#include "thread_sanitizer.h"
#include "ts_util.h"


void PcToStrings(uintptr_t pc, bool demangle,
                 string *img_name, string *rtn_name,
                 string *file_name, int *line_no) {
}

string PcToRtnName(uintptr_t pc, bool demangle) {

}

extern "C" {

void SPut(EventType type, int32_t tid, uintptr_t pc,
          uintptr_t a, uintptr_t info) {
  Event event(type, tid, pc, a, info);
  ThreadSanitizerHandleOneEvent(&event);
}

}
