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


void PcToStrings(uintptr_t pc, bool demangle,
                 string *img_name, string *rtn_name,
                 string *file_name, int *line_no) {
}

string PcToRtnName(uintptr_t pc, bool demangle) {

}

extern "C" {

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

  return true;
}

}


extern "C" {

void finalize() {
  ThreadSanitizerFini();
  if (G_flags->error_exitcode && GetNumberOfFoundErrors() > 0) {
    // This is the last atexit hook, so it's ok to terminate the program.
    _exit(G_flags->error_exitcode);
  }
}

}


extern "C" {

void SPut(EventType type, int32_t tid, uintptr_t pc,
          uintptr_t a, uintptr_t info) {
  Event event(type, tid, pc, a, info);
  ThreadSanitizerHandleOneEvent(&event);
}

}
