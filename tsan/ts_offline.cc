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
static uintptr_t g_current_pc;


//------------- Utils ------------------- {{{1
static EventType EventNameToEventType(const char *name) {
  map<string, int>::iterator it = g_event_type_map->find(name);
  CHECK(it != g_event_type_map->end());
  return (EventType)it->second;
}

static void InitEventTypeMap() {
  g_event_type_map = new map<string, int>;
  for (int i = 0; i < LAST_EVENT; i++) {
    (*g_event_type_map)[kEventNames[i]] = i;
  }
}

void SkipWhiteSpaceAndComments(FILE *file) {
  bool in_comment = false;
  int c = 0;
  while (true) {
    c = fgetc(file);
    if (c == EOF) return;
    if (in_comment) {
      if (c == '\n') {
        in_comment = false;
      }
      continue;
    }
    if (c == '#') {
      in_comment = true;
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
  uintptr_t pc, a, info;
  SkipWhiteSpaceAndComments(file);
  if (5 == fscanf(file, "%s%x%lx%lx%lx", name, &tid, &pc, &a, &info)) {
    event->Init(EventNameToEventType(name), tid, pc, a, info);
    return true;
  }
  return false;
}

void ReadEventsFromFile(FILE *file) {
  Event event;
  while (ReadOneEventFromFile(file, &event)) {
    // event.Print();
    g_current_pc = event.pc();
    ThreadSanitizerHandleOneEvent(&event);
  }
}

//------------- ThreadSanitizer exports ------------ {{{1

void PcToStrings(uintptr_t pc, bool demangle,
                string *img_name, string *rtn_name,
                string *file_name, int *line_no) {
  *img_name = "";
  *rtn_name = "";
  *file_name = "";
  *line_no = 0;
}

string PcToRtnName(uintptr_t pc, bool demangle) {
  return string("unimplemented");
}

uintptr_t GetPcOfCurrentThread() {
  return g_current_pc;
}
//------------- main ---------------------------- {{{1
int main(int argc, char *argv[]) {
  printf("INFO: ThreadSanitizerOffline\n");

  InitEventTypeMap();
  G_flags = new FLAGS;
  vector<string> args(argv + 1, argv + argc);
  ThreadSanitizerParseFlags(&args);
  ThreadSanitizerInit();

  ReadEventsFromFile(stdin);

  ThreadSanitizerFini();
}

// end. {{{1
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
