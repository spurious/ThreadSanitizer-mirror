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


bool in_initialize = false;
typedef uintptr_t pc_t;

const size_t kCallStackReserve = 32;

extern "C" void goCallback(void* p);
extern "C" char* goCallbackPcToRtnName(uintptr_t pc);
extern "C" int goCallbackCommentPc(uintptr_t pc, char **img,
                                   char **rtn, char **file, int *line);

int numEventsRead = 0; // need some estimation
int eventsCount[100000];

extern "C"
void SPut(EventType type, int32_t tid, uintptr_t pc,
          uintptr_t a, uintptr_t info) {
  if (type == THR_START) {
    CallStackPod *__tsan_shadow_stack = new CallStackPod;
    __tsan_shadow_stack->end_ = __tsan_shadow_stack->pcs_ + 32;
    pc = (uintptr_t)__tsan_shadow_stack;
  }
  ++eventsCount[type];
  Event event(type, tid, pc, a, info);
  ThreadSanitizerHandleOneEvent(&event);
  ++numEventsRead;
}

void PcToStrings(uintptr_t pc, bool demangle,
                 string *img_name, string *rtn_name,
                 string *file_name, int *line_no) {
  char *img, *rtn, *file;

  int ok = goCallbackCommentPc(pc, &img, &rtn, &file, line_no);
  if (!ok) return;
  *img_name = string(img);
  *rtn_name = string(rtn);
  *file_name = string(file);
  free(img);
  free(rtn);
  free(file);
}

string PcToRtnName(uintptr_t pc, bool demangle) {
  // TODO: demangle is dropped, is it ok?
  // TODO: turn it on when the Go part works
  //  char* ret = goCallbackPcToRtnName(pc);
  //  return string(ret);
  return "";
}

extern "C"
bool initialize() {
  if (in_initialize) return false;
  in_initialize = true;

  G_flags = new FLAGS;
  vector<string> args;

  char *env = getenv("TSAN_ARGS");
  if (env) {
    string env_args(const_cast<char*>(env));
    size_t start = env_args.find_first_not_of(" ");
    size_t stop = env_args.find_first_of(" ", start);
    while (start != string::npos || stop != string::npos) {
      args.push_back(env_args.substr(start, stop - start));
      start = env_args.find_first_not_of(" ", stop);
      stop = env_args.find_first_of(" ", start);
    }
  }

  ThreadSanitizerParseFlags(&args);
  ThreadSanitizerInit();

  // Depends on when initialize() is called: before goroutine 0
  // was created or after.
  // In current implementation we start before everything, so
  // comment out this line for now
  //  SPut(THR_START, 0, 0, 0, 0);
  return true;
}

extern "C"
void finalize() {
  // TODO(mpimenov): use G_stats->PrintStats() or something like that
  Printf("\nThreadSanitizer has received %d events\n", numEventsRead);
  Printf("%d READs\n", eventsCount[READ]);
  Printf("%d WRITEs\n", eventsCount[WRITE]);
  Printf("%d SIGNALs\n", eventsCount[SIGNAL]);
  Printf("%d WAITs\n", eventsCount[WAIT]);
  Printf("%d RTN_CALLs\n", eventsCount[RTN_CALL]);
  Printf("%d RTN_EXITs\n", eventsCount[RTN_EXIT]);
  Printf("\n\n");

  ThreadSanitizerFini();

  if (GetNumberOfFoundErrors() > 0) {
    _exit(1);
  }
}
