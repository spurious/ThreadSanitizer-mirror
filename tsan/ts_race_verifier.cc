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

// Author: Evgeniy Stepanov.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>
#include <set>
#include <iterator>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <fstream>

#include "ts_lock.h"
#include "ts_util.h"
#include "ts_race_verifier.h"
#include "thread_sanitizer.h"

struct PossibleRace {
  // racy instruction
  uintptr_t pc;
  // concurrent traces
  vector<uintptr_t> traces;
  // report text
  string report;
  // whether this race has already been reported
  bool reported;
};

// pc -> race
static map<uintptr_t, PossibleRace*>* races_map;

// Data about a call site.
struct CallSite {
  int thread_id;
  uintptr_t pc;
};

struct TypedCallSites {
  vector<CallSite> reads;
  vector<CallSite> writes;
};

// data address -> ([write callsites], [read callsites])
typedef map<uintptr_t, TypedCallSites> AddressMap;

static TSLock racecheck_lock;
static AddressMap* racecheck_map;
// data addresses that are ignored (they have already been reported)
static set<uintptr_t>* ignore_addresses;

static int n_reports;

/**
 * Given max and min pc of a trace (both inclusive!), returns whether this trace
 * is interesting to us at all (via the return value), and whether it should be
 * instrumented fully (*instrument_pc=0), or 1 instruction only. In the latter
 * case, *instrument_pc contains the address of the said instruction.
 */
bool RaceVerifierGetAddresses(uintptr_t min_pc, uintptr_t max_pc,
    uintptr_t* instrument_pc) {
  uintptr_t pc = 0;
  for (map<uintptr_t, PossibleRace*>::iterator it = races_map->begin();
       it != races_map->end(); ++it) {
    PossibleRace* race = it->second;
    if (race->reported)
      continue;
    if (race->pc >= min_pc && race->pc <= max_pc) {
      if (pc) {
        // Two race candidates in one trace. Just instrument it fully.
        *instrument_pc = 0;
        return true;
      }
      pc = race->pc;
    }
    for (vector<uintptr_t>::iterator it2 = race->traces.begin();
         it2 != race->traces.end(); ++it2) {
      if (*it2 >= min_pc && *it2 <= max_pc) {
        *instrument_pc = 0;
        return true;
      }
    }
  }
  *instrument_pc = pc;
  return !!pc;
}

/* Build and print a race report for a data address. Does not print stack traces
   and symbols and all the fancy stuff - we don't have that info. Used when we
   don't have a ready report - for unexpected races and for
   --race-verifier-extra races.

   racecheck_lock must be held by the current thread.
*/
static void PrintRaceReportEmpty(uintptr_t addr) {
  TypedCallSites* typedCallSites = &(*racecheck_map)[addr];
  vector<CallSite>& writes = typedCallSites->writes;
  vector<CallSite>& reads = typedCallSites->reads;
  for (vector<CallSite>::const_iterator it = writes.begin();
       it != writes.end(); ++ it) {
    Printf("  write at %p\n", it->pc);
  }
  for (vector<CallSite>::const_iterator it = reads.begin();
       it != reads.end(); ++ it) {
    Printf("  read at %p\n", it->pc);
  }
}

/* Find a PossibleRace that matches current accesses (racecheck_map) to the
   given data address.

   racecheck_lock must be held by the current thread.
 */
static PossibleRace* FindRaceForAddr(uintptr_t addr) {
  TypedCallSites* typedCallSites = &(*racecheck_map)[addr];
  vector<CallSite>& writes = typedCallSites->writes;
  vector<CallSite>& reads = typedCallSites->reads;
  for (vector<CallSite>::const_iterator it = writes.begin();
       it != writes.end(); ++ it) {
    map<uintptr_t, PossibleRace*>::iterator it2 = races_map->find(it->pc);
    if (it2 != races_map->end())
      return it2->second;
  }
  for (vector<CallSite>::const_iterator it = reads.begin();
       it != reads.end(); ++ it) {
    map<uintptr_t, PossibleRace*>::iterator it2 = races_map->find(it->pc);
    if (it2 != races_map->end())
      return it2->second;
  }
  return NULL;
}

/* Prints a race report for the given data address, either finding one in a
   matching PossibleRace, or just printing pc's of the mops.

   racecheck_lock must be held by the current thread.
*/
static void PrintRaceReport(uintptr_t addr) {
  PossibleRace* race = FindRaceForAddr(addr);
  if (race) {
    ExpectedRace* expected_race = ThreadSanitizerFindExpectedRace(addr);
    if (expected_race)
      expected_race->count++;
    bool is_expected = !!expected_race;
    bool is_unverifiable = is_expected && !expected_race->is_verifiable;

    if (is_expected && !is_unverifiable && !G_flags->show_expected_races)
      return;

    if (is_unverifiable)
      Printf("WARNING: Confirmed a race that was marked as UNVERIFIABLE:\n");
    else
      Printf("WARNING: Confirmed a race:\n");
    const string& report = race->report;
    if (report.empty()) {
      PrintRaceReportEmpty(addr);
    } else {
      Printf("%s", report.c_str());
    }
    // Suppress future reports for this race.
    race->reported = true;
    ignore_addresses->insert(addr);

    n_reports++;
  } else {
    Printf("Warning: unexpected race found!\n");
    PrintRaceReportEmpty(addr);
  }
}

/**
 * This function is called before the mop delay.
 * @param thread_id Thread id.
 * @param addr Data address.
 * @param pc Instruction pc.
 * @param is_w Whether this is a write (true) or a read (false).
 * @return True if this access is interesting to us at all. If true, the caller
 *     should delay and then call RaceVerifierEndAccess. If false, it should do
 *     nothing more for this mop.
 */
bool RaceVerifierStartAccess(int thread_id, uintptr_t addr, uintptr_t pc,
    bool is_w) {
  CallSite callSite;
  callSite.thread_id = thread_id;
  callSite.pc = pc;
  racecheck_lock.Lock();

  if (debug_race_verifier)
    Printf("[%d] pc %p %s addr %p start\n", thread_id, pc,
        is_w ? "write" : "read", addr);

  if (ignore_addresses->count(addr)) {
    racecheck_lock.Unlock();
    return false;
  }

  TypedCallSites* typedCallSites = &(*racecheck_map)[addr];
  vector<CallSite>& writes = typedCallSites->writes;
  vector<CallSite>& reads = typedCallSites->reads;
  (is_w ? writes : reads).push_back(callSite);
  if (writes.size() > 0 && writes.size() + reads.size() > 1) {
    // race!
    PrintRaceReport(addr);
  }
  racecheck_lock.Unlock();
  return true;
}

/* This function is called after the mop delay, only if RaceVerifierStartAccess
   returned true. The arguments are exactly the same. */
void RaceVerifierEndAccess(int thread_id, uintptr_t addr, uintptr_t pc,
    bool is_w) {
  racecheck_lock.Lock();

  if (debug_race_verifier)
    Printf("[%d] pc %p %s addr %p end\n", thread_id, pc,
        is_w ? "write" : "read", addr);
  if (ignore_addresses->count(addr)) {
    racecheck_lock.Unlock();
    return;
  }

  TypedCallSites* typedCallSites = &(*racecheck_map)[addr];
  vector<CallSite>& vec =
      is_w ? typedCallSites->writes : typedCallSites->reads;
  for (int i = vec.size() - 1; i >= 0; --i) {
    if (vec[i].thread_id == thread_id) {
      vec.erase(vec.begin() + i);
      break;
    }
  }
  racecheck_lock.Unlock();
}

/* Parse a race description that appears in TSan logs after the words
   "Race verifier data: ", not including the said words. It looks like
   "pc,trace[,trace]...", without spaces. */
static PossibleRace* ParseRaceInfo(const string& raceInfo) {
  std::istringstream iss(raceInfo);
  PossibleRace* race = new PossibleRace();
  iss >> std::hex;
  iss >> race->pc;
  while (iss.good()) {
    char delim;
    iss >> delim;
    if (delim != ',')
      return NULL;
    uintptr_t trace;
    iss >> trace;
    race->traces.push_back(trace);
  }
  race->reported = false;
  Printf("Possible race: %s\n", raceInfo.c_str());
  return race;
}

/* Parse a race description and add it to races_map. */
static void RaceVerifierParseRaceInfo(const string& raceInfo) {
  PossibleRace* race = ParseRaceInfo(raceInfo);
  if (race)
    (*races_map)[race->pc] = race;
  else
    Printf("Bad raceInfo: %s\n", raceInfo.c_str());
}

/* Parse a TSan log and add all race verifier info's from it to our storage of
   possible races. */
static void RaceVerifierParseFile(const string& fileName) {
  std::fstream fs(fileName.c_str(), std::ios_base::in);
  string line;
  std::ostringstream* os = NULL;
  const string RACEINFO_MARKER = "Race verifier data: ";
  while (fs.good()) {
    getline(fs, line);
    size_t pos;
    if ((line.find("WARNING: Possible data race during") !=
            string::npos) ||
        (line.find("WARNING: Expected data race during") !=
            string::npos)) {
      os = new std::ostringstream();
      (*os) << line << std::endl;
    } else if ((pos = line.find(RACEINFO_MARKER)) != string::npos) {
      pos += RACEINFO_MARKER.size();
      string raceInfo = line.substr(pos);
      PossibleRace* race = ParseRaceInfo(raceInfo);
      (*os) << "}}}" << std::endl;
      race->report = os->str();
      (*races_map)[race->pc] = race;
      delete os;
      os = NULL;
    } else if (os) {
      (*os) << line << std::endl;
    }
  }
  // TODO(eugenis): check for I/O errors
}

/**
 * Init the race verifier. Should be called exactly once before any other
 * functions in this file.
 * @param fileNames Names of TSan log to parse.
 * @param raceInfos Additional race description strings.
 */
void RaceVerifierInit(const vector<string>& fileNames,
    const vector<string>& raceInfos) {
  races_map = new map<uintptr_t, PossibleRace*>();
  racecheck_map = new AddressMap();
  ignore_addresses = new set<uintptr_t>();

  for (vector<string>::const_iterator it = fileNames.begin();
       it != fileNames.end(); ++it) {
    RaceVerifierParseFile(*it);
  }
  for (vector<string>::const_iterator it = raceInfos.begin();
       it != raceInfos.end(); ++it) {
    RaceVerifierParseRaceInfo(*it);
  }
}

void RaceVerifierFini() {
  Report("RaceVerifier summary: verified %d race(s)\n", n_reports);
  int n_errors = GetNumberOfFoundErrors();
  SetNumberOfFoundErrors(n_errors + n_reports);
}
