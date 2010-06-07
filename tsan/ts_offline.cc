/*
  This file is part of ThreadSanitizer, a dynamic data race detector.

  Copyright (C) 2008-2009 Google Inc
     opensource@google.com

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License as
  published by the Free Software Foundation; either version 2 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
  02111-1307, USA.

  The GNU General Public License is contained in the file COPYING.
*/

// Author: Konstantin Serebryany.
// Author: Timur Iskhodzhanov.

// Experimental off-line race detector.
// Reads program events from a file and detects races.
// See http://code.google.com/p/data-race-test

// ------------- Includes ------------- {{{1
#include "thread_sanitizer.h"

#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>

// ------------- Globals ------------- {{{1
static map<string, int> *g_event_type_map;
struct PcInfo {
  string img_name;
  string file_name;
  string rtn_name;
  int line;
};

static map<uintptr_t, PcInfo> *g_pc_info_map;

//------------- Utils ------------------- {{{1
static EventType EventNameToEventType(const char *name) {
  map<string, int>::iterator it = g_event_type_map->find(name);
  if (it == g_event_type_map->end()) {
    Printf("Unknown event type: %s\n", name);
  }
  CHECK(it != g_event_type_map->end());
  return (EventType)it->second;
}

static void InitEventTypeMap() {
  g_event_type_map = new map<string, int>;
  for (int i = 0; i < LAST_EVENT; i++) {
    (*g_event_type_map)[kEventNames[i]] = i;
  }
}

static void SkipCommentText(FILE *file) {
  const int kBufSize = 1000;
  char buff[kBufSize];
  int i = 0;
  while (true) {
    int c = fgetc(file);
    if (c == EOF) break;
    if (c == '\n')  break;
    if (i < kBufSize - 1)
      buff[i++] = c;
  }
  buff[i] = 0;
  if (buff[0] == 'P' && buff[1] == 'C') {
    char img[kBufSize];
    char rtn[kBufSize];
    char file[kBufSize];
    int line = 0;
    unsigned long pc = 0;
    if (sscanf(buff, "PC %lx %s %s %s %d", (unsigned long*)&pc,
               img, rtn, file, &line) == 5 &&
        pc != 0) {
      CHECK(g_pc_info_map);
      PcInfo pc_info;
      pc_info.img_name = img;
      pc_info.rtn_name = rtn;
      pc_info.file_name = file;
      pc_info.line = line;
      (*g_pc_info_map)[pc] = pc_info;
      // Printf("***** PC %lx %s\n", pc, rtn);
    }
  }
  if (buff[0] == '>') {
    // Just print the rest of comment.
    Printf("%s\n", buff + 1);
  }
}

static void SkipWhiteSpaceAndComments(FILE *file) {
  int c = 0;
  while (true) {
    c = fgetc(file);
    if (c == EOF) return;
    if (c == '#' || c == '=') {
      SkipCommentText(file);
      continue;
    }
    if (isspace(c)) continue;
    break;
  }
  ungetc(c, file);
}

bool ReadOneEventFromFile(FILE *file, Event *event) {
  CHECK(event);
  char name[1024];
  uint32_t tid;
  unsigned long pc, a, info;
  SkipWhiteSpaceAndComments(file);
  if (5 == fscanf(file, "%s%x%lx%lx%lx", name, &tid, &pc, &a, &info)) {
    event->Init(EventNameToEventType(name), tid, pc, a, info);
    return true;
  }
  return false;
}

static const uint32_t max_unknown_thread = 10000;

static bool known_threads[max_unknown_thread] = {};

void ReadEventsFromFile(FILE *file) {
  Event event;
  int n_events = 0;
  while (ReadOneEventFromFile(file, &event)) {
    // event.Print();
    n_events++;
    uint32_t tid = event.tid();
    if (event.type() == THR_START && tid < max_unknown_thread) {
      known_threads[tid] = true;
    }
    if (tid >= max_unknown_thread || known_threads[tid]) {
      ThreadSanitizerHandleOneEvent(&event);
    }
  }
  Printf("INFO: ThreadSanitizerOffline: %d events read\n", n_events);
}
//------------- ThreadSanitizer exports ------------ {{{1

void PcToStrings(uintptr_t pc, bool demangle,
                string *img_name, string *rtn_name,
                string *file_name, int *line_no) {
  if (g_pc_info_map->count(pc) == 0) {
    *img_name = "";
    *rtn_name = "";
    *file_name = "";
    *line_no = 0;
    return;
  }
  PcInfo &info = (*g_pc_info_map)[pc];
  *img_name = info.img_name;
  *rtn_name = info.rtn_name;
  *file_name = info.file_name;
  *line_no = info.line;
  if (*file_name == "unknown")
    *file_name = "";
}

string PcToRtnName(uintptr_t pc, bool demangle) {
  return string("unimplemented");
}
//------------- main ---------------------------- {{{1
int main(int argc, char *argv[]) {
  printf("INFO: ThreadSanitizerOffline r%s\n", TS_VERSION);

  InitEventTypeMap();
  g_pc_info_map = new map<uintptr_t, PcInfo>;
  G_flags = new FLAGS;

  vector<string> args(argv + 1, argv + argc);
  ThreadSanitizerParseFlags(&args);
  ThreadSanitizerInit();

  CHECK(G_flags);
  ReadEventsFromFile(stdin);

  ThreadSanitizerFini();
}

// -------- thread_sanitizer.cc -------------------------- {{{1
// ... for performance reasons...
#ifdef INCLUDE_THREAD_SANITIZER_CC
# undef INCLUDE_THREAD_SANITIZER_CC
# include "thread_sanitizer.cc"
#endif

// end. {{{1
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
