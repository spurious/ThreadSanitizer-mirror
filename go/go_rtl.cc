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

/*
static struct sigaction signal_actions[NSIG];  // protected by GIL
static __thread siginfo_t pending_signals[NSIG];
typedef enum { PSF_NONE = 0, PSF_SIGNAL, PSF_SIGACTION } pending_signal_flag_t;
static __thread pending_signal_flag_t pending_signal_flags[NSIG];
static __thread bool have_pending_signals;
*/

extern "C" void goCallback(void* p);
extern "C" char* goCallbackPcToRtnName(uintptr_t pc);
extern "C" int goCallbackCommentPc(uintptr_t pc, char **img, char **rtn, char **file, int *line);
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
  /*  char* ret = goCallbackPcToRtnName(pc); // TODO: demangle is dropped, is it ok?
  return string(ret);
  */
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

  // Depends on when initialize() is called: before goroutine 0 was created or not
  // In current implementation we start before everything, so
  // comment out this line for now
  // SPut(THR_START, 0, 0, 0, 0);


  /*

  if (G_flags->dry_run) {
    Printf("WARNING: the --dry_run flag is not supported anymore. "
           "Ignoring.\n");
  }

  //  static_tls_size = GetTlsSize();
  memset(__tsan_shadow_stack.pcs_, 0, kCallStackReserve * sizeof(__tsan_shadow_stack.pcs_[0]));
  __tsan_shadow_stack.end_ = __tsan_shadow_stack.pcs_ + kCallStackReserve;
  in_initialize = false;

  // Get the stack size and stack top for the current thread.
  // TODO(glider): do something if pthread_getattr_np() is not supported.
  pthread_attr_t attr;
  size_t stack_size = 8 << 20;  // 8M
  void *stack_bottom = NULL;

  for (int sig = 0; sig < NSIG; sig++) {
    pending_signal_flags[sig] = PSF_NONE;
  }

  SPut(THR_START, 0, (pc_t) &__tsan_shadow_stack, 0, 0);
  //SPut(THR_STACK_TOP, 0, 0, (uintptr_t)&stack_size, stack_size);

*/
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

  if (G_flags->error_exitcode && GetNumberOfFoundErrors() > 0) {
    // This is the last atexit hook, so it's ok to terminate the program.
    _exit(G_flags->error_exitcode);
  }
}
