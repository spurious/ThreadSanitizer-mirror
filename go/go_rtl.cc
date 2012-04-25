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

typedef uintptr_t pc_t;

const size_t kCallStackReserve = 32;
bool g_initialized = false;

extern "C" char* goCallbackPcToRtnName(uintptr_t pc);
extern "C" int goCallbackCommentPc(uintptr_t pc, char **img,
                                   char **rtn, char **file, int *line);

// We need some estimation of the number of events
// that we receive for Go.
const int kNumEvents = sizeof(kEventNames)/sizeof(kEventNames[0]);
int g_numEventsRead = 0;
int g_eventsCount[kNumEvents];

extern "C"
void SPut(EventType type, int32_t tid, uintptr_t pc,
          uintptr_t a, uintptr_t info) {
  if (type == THR_START) {
    CallStackPod *__tsan_shadow_stack = new CallStackPod;
    __tsan_shadow_stack->end_ = __tsan_shadow_stack->pcs_ + kCallStackReserve;
    memset(__tsan_shadow_stack->pcs_, 0, kCallStackReserve * sizeof(__tsan_shadow_stack->pcs_[0]));
    pc = (uintptr_t)__tsan_shadow_stack;
  }

  ++g_eventsCount[type];
  ++g_numEventsRead;
  Event event(type, tid, pc, a, info);
  ThreadSanitizerHandleOneEvent(&event);
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
  //char* ret = goCallbackPcToRtnName(pc);
  // return string(ret);
  return "";
}

extern "C"
bool initialize() {
  if (g_initialized) return false;
  g_initialized = true;

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
  /*
  TODO(mpimenov): think about using G_stats->PrintStats()
  Possible snippet for the number of reads (doesn't work because thread_sanitizer.h
  does not list all TSanThread's methods)

  int cnt = 0;
  for (int i = 0; i < TSanThread::NumberOfThreads(); i++) {
    TSanThread *t = TSanThread::Get(TID(i));
    cnt += t->stats.events[READ];
  }
  Printf("-- %d\n", cnt);
  */

  EventType interesting[] = {
    THR_START,
    READ,
    WRITE,
    SIGNAL,
    WAIT,
    RTN_CALL,
    RTN_EXIT,
    MALLOC,
    FREE,
  };
  Printf("\nThreadSanitizer has received %d events\n", g_numEventsRead);
  int numInteresting = sizeof(interesting)/sizeof(interesting[0]);
  for (int i = 0; i < numInteresting; i++) {
    EventType e = interesting[i];
    Printf("%d %ss\n", g_eventsCount[e], kEventNames[e]);
  }
  Printf("\n\n");

  ThreadSanitizerFini();

  if (GetNumberOfFoundErrors() > 0) {
    // TODO(mpimenov): replace with _exit(1)
    // after all src/pkg tests have passed
    _exit(0);
  }
}
