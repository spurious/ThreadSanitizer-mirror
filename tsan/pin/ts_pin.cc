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
//
// ******* WARNING ********
// This code is experimental. Do not expect anything here to work.
// ***** END WARNING ******

#include "pin.H"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <assert.h>

#ifdef NDEBUG
# error "Please don't define NDEBUG"
#endif
#define CHECK assert

//--------------- Aux classes ------------- {{{1
struct Routine {
  string rtn_name;
};

//--------------- Globals ----------------- {{{1
static FILE *G_out = stdout;

// Number of threads created by pthread_create (i.e. not counting main thread).
static int n_created_threads = 0;

// Maps address of rtn entry/ret to Routine*
static map<uintptr_t, Routine*> *routine_address_map;

static int64_t dyn_read_count;
static int64_t dyn_write_count;

static bool main_entered, main_exited;

int kVerbosity = 1;
bool kIgnoreStack = true;

//--------- Misc functions ----------- {{{1
void Printf(const char *format, ...) {
  va_list args;
  va_start(args, format);
  vfprintf(G_out, format, args);
  fflush(G_out);
  va_end(args);
}

//--------- Instrumentation callbacks --------------- {{{1
//--------- Threads --------------------------------- {{{2
void CallbackForThreadStart(THREADID tid, CONTEXT *ctxt, 
                            INT32 flags, void *v) {
  Printf("T%d %s\n", tid, __FUNCTION__);
}

void CallbacForThreadFini(THREADID tid, const CONTEXT *ctxt,
                          INT32 code, void *v) {
  Printf("T%d %s\n", tid, __FUNCTION__);
}


static void Before_pthread_join(THREADID tid, ADDRINT pc, 
                                ADDRINT arg1, ADDRINT arg2) {
  Printf("T%d %s\n", tid, __FUNCTION__);
}
static void After_pthread_join(THREADID tid, ADDRINT pc, ADDRINT retval) {
  Printf("T%d %s\n", tid, __FUNCTION__);
}
static void Before_pthread_create(THREADID tid, ADDRINT pc,
                                  ADDRINT arg1, ADDRINT arg2, 
                                  ADDRINT arg3, ADDRINT arg4) {
  Printf("T%d %s\n", tid, __FUNCTION__);
  n_created_threads++;
}
static void After_pthread_create(THREADID tid, ADDRINT pc, ADDRINT retval) {
  Printf("T%d %s\n", tid, __FUNCTION__);
}

//--------- main() --------------------------------- {{{2
void Before_main(THREADID tid, ADDRINT pc, ADDRINT argc, ADDRINT argv) {
  CHECK(tid == 0);
  Printf("T%d %s\n", tid, __FUNCTION__);
  main_entered = true;
}

void After_main(THREADID tid, ADDRINT pc) {
  CHECK(tid == 0);
  Printf("T%d %s\n", tid, __FUNCTION__);
  main_exited = true;
}

//--------- memory allocation ---------------------- {{{2
void Before_malloc(THREADID tid, ADDRINT pc, ADDRINT size) {
  if (kVerbosity >= 1 && main_entered && !main_exited && tid == 0)
    Printf("T%d %s: size=%ld\n", tid, __FUNCTION__, size);
}
void After_malloc(THREADID tid, ADDRINT pc, ADDRINT retval) {
  if (kVerbosity >= 1 && main_entered && !main_exited && tid == 0)
    Printf("T%d %s: ret=%p\n", tid, __FUNCTION__, retval);
}
void Before_free(THREADID tid, ADDRINT pc, ADDRINT ptr) {
  if (kVerbosity >= 1 && main_entered && !main_exited && tid == 0)
    Printf("T%d %s; ptr=%p\n", tid, __FUNCTION__, ptr);
}

//-------- Routines and stack ---------------------- {{{2
void InsertBeforeEvent_RoutineEntry(THREADID tid, ADDRINT pc,
                                    ADDRINT sp, Routine *routine) {
  if (kVerbosity >= 2 && main_entered && !main_exited)
    Printf("T%d %s: %s\n", tid, __FUNCTION__, routine->rtn_name.c_str());
}

static void InsertAfterEvent_RoutineExit(THREADID tid, ADDRINT pc, ADDRINT sp) {
//  Printf("T%d %s\n", tid, __FUNCTION__);
}
void InsertBeforeEvent_Call(THREADID tid, ADDRINT pc, ADDRINT target) {
//  Printf("T%d %s\n", tid, __FUNCTION__);
}

void InsertBeforeEvent_SblockEntry(THREADID tid, ADDRINT pc) {
//  Printf("T%d %s\n", tid, __FUNCTION__);
}

static void InsertAfterEvent_SpUpdate(THREADID tid, ADDRINT pc, ADDRINT sp) {
}

//---------- Memory accesses -------------------------- {{{2
static void InsertBeforeEvent_MemoryRead(THREADID tid, ADDRINT pc,
                                         ADDRINT a, ADDRINT size) {
  dyn_read_count++;
}


static void InsertBeforeEvent_MemoryWrite(THREADID tid, ADDRINT pc,
                                          ADDRINT a, ADDRINT size) {
  dyn_write_count++;
}

//---------- Synchronization -------------------------- {{{2
static void Before_pthread_mutex_unlock(THREADID tid, ADDRINT pc,
                                        ADDRINT mu) {
  Printf("T%d %s\n", tid, __FUNCTION__);
}

static void Before_pthread_mutex_lock(THREADID tid, ADDRINT pc, 
                                      ADDRINT mu) {
  Printf("T%d %s\n", tid, __FUNCTION__);
}
static void After_pthread_mutex_lock(THREADID tid, ADDRINT pc, 
                                     ADDRINT unused) {
  Printf("T%d %s\n", tid, __FUNCTION__);
}
static void After_pthread_mutex_trylock(THREADID tid, ADDRINT pc, 
                                        ADDRINT retval) {
  Printf("T%d %s\n", tid, __FUNCTION__);
}


static void Before_pthread_barrier_wait(THREADID tid, ADDRINT pc,
                                        ADDRINT barrier) {
}
static void After_pthread_barrier_wait(THREADID tid, ADDRINT pc) {
}

static void Before_pthread_cond_signal(THREADID tid, ADDRINT pc, ADDRINT cv) {
}
static void Before_pthread_cond_wait(THREADID tid, ADDRINT pc,
                                     ADDRINT cv, ADDRINT mu) {
}
static void After_pthread_cond_wait(THREADID tid, ADDRINT pc) {
}
static void After_pthread_cond_timedwait(THREADID tid, ADDRINT pc, 
                                         ADDRINT retval) {
}

static void Before_sem_post(THREADID tid, ADDRINT pc, ADDRINT sem) {
}
static void Before_sem_wait(THREADID tid, ADDRINT pc, ADDRINT sem) {
}
static void After_sem_wait(THREADID tid, ADDRINT pc) {
}
static void After_sem_trywait(THREADID tid, ADDRINT pc, ADDRINT retval) {
}


//--------- Instrumentation ----------------------- {{{1
static bool IgnoreImage(IMG img) {
  string name = IMG_Name(img);
  if (name.find("/ld-") != string::npos)
    return true;
  return false;
}

static bool IgnoreRtn(RTN rtn) {
  CHECK(rtn != RTN_Invalid());
  // (RTN_Name(rtn));
  return false;
}

static void InstrumentRead(INS ins) {
  INS_InsertCall(ins, IPOINT_BEFORE,
                 (AFUNPTR)InsertBeforeEvent_MemoryRead,
                 IARG_THREAD_ID, IARG_INST_PTR,
                 IARG_MEMORYREAD_EA, IARG_MEMORYREAD_SIZE,
                 IARG_END);
}

static void InstrumentWrite(INS ins) {
  INS_InsertCall(ins, IPOINT_BEFORE,
                 (AFUNPTR)InsertBeforeEvent_MemoryWrite,
                 IARG_THREAD_ID, IARG_INST_PTR,
                 IARG_MEMORYWRITE_EA, IARG_MEMORYWRITE_SIZE,
                 IARG_END);
}

static void InstrumentRead2(INS ins) {
  INS_InsertCall(ins, IPOINT_BEFORE,
                 (AFUNPTR)InsertBeforeEvent_MemoryRead,
                 IARG_THREAD_ID, IARG_INST_PTR,
                 IARG_MEMORYREAD2_EA, IARG_MEMORYREAD_SIZE,
                 IARG_END);
}

static void InstrumentBbl(BBL bbl, RTN rtn, bool ignore_memory) {
  INS tail = BBL_InsTail(bbl);

  // All memory reads/writes
  for( INS ins = BBL_InsHead(bbl); 
       !ignore_memory && INS_Valid(ins);
       ins = INS_Next(ins) ) {
    if (ins != tail) {
      CHECK(!INS_IsRet(ins));
      CHECK(!INS_IsProcedureCall(ins));
    }
    bool is_stack = INS_IsStackRead(ins) || INS_IsStackWrite(ins);
    bool is_atomic = INS_IsAtomicUpdate(ins);
    bool is_read  = INS_IsMemoryRead(ins);
    bool is_read2 = INS_HasMemoryRead2(ins);
    bool is_write = INS_IsMemoryWrite(ins);

    // SP update.
    if (INS_RegWContain(ins, REG_STACK_PTR)) {
      INS_InsertCall(ins, IPOINT_BEFORE, (AFUNPTR)InsertAfterEvent_SpUpdate,
                     IARG_THREAD_ID, IARG_INST_PTR,
                     IARG_REG_VALUE, REG_STACK_PTR,
                     IARG_END);

    }
    if (kIgnoreStack && is_stack) continue;
    if (is_atomic) continue;
    if (is_read)  InstrumentRead(ins);
    if (is_read2) InstrumentRead2(ins);
    if (is_write) InstrumentWrite(ins);
  }

  // Call.
  if (INS_IsProcedureCall(tail) && !INS_IsSyscall(tail)) {
    INS_InsertCall(tail, IPOINT_BEFORE,
                   (AFUNPTR)InsertBeforeEvent_Call,
                   IARG_THREAD_ID, IARG_INST_PTR, 
                   IARG_BRANCH_TARGET_ADDR,
                   IARG_END);
  }

  if (INS_IsRet(tail)) {
    INS_InsertCall(tail, IPOINT_BEFORE, 
                   (AFUNPTR)InsertAfterEvent_RoutineExit, 
                   IARG_THREAD_ID, IARG_INST_PTR,
                   IARG_REG_VALUE, REG_STACK_PTR,
                   IARG_END);
  }
}

void CallbackForTRACE(TRACE trace, void *v) {
  RTN rtn = TRACE_Rtn(trace);
  bool ignore_memory = false;
  string img_name = "<>";
  string rtn_name = "<>";
  if (RTN_Valid(rtn)) {
    SEC sec = RTN_Sec(rtn);
    IMG img = SEC_Img(sec);
    rtn_name = RTN_Name(rtn);
    img_name = IMG_Name(img);

    if (IgnoreImage(img)) {
      // Printf("Ignoring memory accesses in %s\n", IMG_Name(img).c_str());
      ignore_memory = true;
    } else if (IgnoreRtn(rtn)) {
      ignore_memory = true;
    }
  }

  // Handle the head of the trace
  uintptr_t address = TRACE_Address(trace);
  map<uintptr_t, Routine*>::iterator it =
      routine_address_map->find(address);
  if (it != routine_address_map->end()) {
    // If this trace is a routine entrace, place RTN_ENTER
    Routine *routine = it->second;
    TRACE_InsertCall(trace, IPOINT_BEFORE, 
                     (AFUNPTR)InsertBeforeEvent_RoutineEntry, 
                     IARG_THREAD_ID, 
                     IARG_INST_PTR, 
                     IARG_REG_VALUE, REG_STACK_PTR,
                     IARG_PTR, routine,
                     IARG_END);
    // Printf("TRACE: head of rtn %s\n", routine->name());
  } else {
    // Otherwise place SBLOCK_ENTER (only if we are tracking access history)
    TRACE_InsertCall(trace, IPOINT_BEFORE, 
                     (AFUNPTR)InsertBeforeEvent_SblockEntry, 
                     IARG_THREAD_ID, IARG_INST_PTR, 
                     IARG_END);
  }

  for(BBL bbl = TRACE_BblHead(trace); BBL_Valid(bbl); bbl = BBL_Next(bbl)) {
    InstrumentBbl(bbl, rtn, ignore_memory);
  }
}

static bool RtnMatchesName(const string &rtn_name, const string &name) {
  CHECK(name.size() > 0);
  size_t pos = rtn_name.find(name);
  if (pos == string::npos) {
    return false;
  }
  if (pos == 0 && name.size() == rtn_name.size()) {
  //  Printf("Full match: %s %s\n", rtn_name.c_str(), name.c_str());
    return true;
  }
  if (pos == 0 && name.size() < rtn_name.size()
      && rtn_name[name.size()] == '@') {
  //  Printf("Versioned match: %s %s\n", rtn_name.c_str(), name.c_str());
    return true;
  }
  return false;
}

#define INSERT_FN_HELPER(point, name, rtn, to_insert, args...) \
    RTN_Open(rtn); \
    if (kVerbosity >= 2) Printf("RTN: Inserting %-50s (%s) %s (%s) img: %s\n", \
    #to_insert, #point, RTN_Name(rtn).c_str(), name, IMG_Name(img).c_str());\
    RTN_InsertCall(rtn, point, (AFUNPTR)to_insert, IARG_THREAD_ID, \
                   IARG_INST_PTR, args, IARG_END);\
    RTN_Close(rtn); \

#define INSERT_FN(point, name, to_insert, args...) \
  while (RtnMatchesName(rtn_name, name)) {\
    INSERT_FN_HELPER(point, name, rtn, to_insert, args); \
    break;\
  }\

#define INSERT_BEFORE_FN(name, to_insert, args...) \
    INSERT_FN(IPOINT_BEFORE, name, to_insert, args)

#define INSERT_BEFORE_0(name, to_insert) \
    INSERT_BEFORE_FN(name, to_insert, IARG_END);

#define INSERT_BEFORE_1(name, to_insert) \
    INSERT_BEFORE_FN(name, to_insert, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 0)

#define INSERT_BEFORE_2(name, to_insert) \
    INSERT_BEFORE_FN(name, to_insert, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 0, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 1)

#define INSERT_BEFORE_3(name, to_insert) \
    INSERT_BEFORE_FN(name, to_insert, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 0, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 1, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 2)

#define INSERT_BEFORE_4(name, to_insert) \
    INSERT_BEFORE_FN(name, to_insert, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 0, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 1, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 2, \
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 3)

#define INSERT_AFTER_FN(name, to_insert, args...) \
    INSERT_FN(IPOINT_AFTER, name, to_insert, args)

#define INSERT_AFTER_0(name, to_insert) \
    INSERT_AFTER_FN(name, to_insert, IARG_END)

#define INSERT_AFTER_1(name, to_insert) \
    INSERT_AFTER_FN(name, to_insert, IARG_FUNCRET_EXITPOINT_VALUE)

static void MaybeInstrumentOneRoutine(IMG img, RTN rtn) {
  string rtn_name = RTN_Name(rtn);
  if (kVerbosity >= 2) {
    Printf("%s: %s\n", __FUNCTION__, rtn_name.c_str());
  }

  // main()
  INSERT_BEFORE_2("main", Before_main);
  INSERT_AFTER_0("main", After_main);

  // malloc/free
  INSERT_BEFORE_1("malloc", Before_malloc);
  INSERT_AFTER_1("malloc", After_malloc);

  INSERT_BEFORE_1("free", Before_free);

  // pthread create/join
  INSERT_BEFORE_4("pthread_create", Before_pthread_create);
  INSERT_AFTER_1("pthread_create", After_pthread_create);
  INSERT_BEFORE_2("pthread_join", Before_pthread_join);
  INSERT_AFTER_1("pthread_join", After_pthread_join);
  
  // pthread_mutex_*
  INSERT_BEFORE_1("pthread_mutex_unlock", Before_pthread_mutex_unlock);

  INSERT_BEFORE_1("pthread_mutex_lock", Before_pthread_mutex_lock);
  INSERT_AFTER_1("pthread_mutex_lock", After_pthread_mutex_lock);

  INSERT_BEFORE_1("pthread_mutex_trylock", Before_pthread_mutex_lock);
  INSERT_AFTER_1("pthread_mutex_trylock", After_pthread_mutex_trylock);

  // pthread_cond_*
  INSERT_BEFORE_1("pthread_cond_signal", Before_pthread_cond_signal);
  INSERT_BEFORE_2("pthread_cond_wait", Before_pthread_cond_wait);
  INSERT_AFTER_0("pthread_cond_wait", After_pthread_cond_wait);

  INSERT_BEFORE_2("pthread_cond_timedwait", Before_pthread_cond_wait);
  INSERT_AFTER_1("pthread_cond_timedwait", After_pthread_cond_timedwait);

  // pthread_barrier_*
  INSERT_BEFORE_1("pthread_barrier_wait", Before_pthread_barrier_wait);
  INSERT_AFTER_0("pthread_barrier_wait", After_pthread_barrier_wait);

  // sem_*
  INSERT_BEFORE_1("sem_post", Before_sem_post);
  INSERT_BEFORE_1("sem_wait", Before_sem_wait);
  INSERT_AFTER_0("sem_wait", After_sem_wait);
  INSERT_BEFORE_1("sem_trywait", Before_sem_wait);
  INSERT_AFTER_1("sem_trywait", After_sem_trywait);
}

// Pin calls this function every time a new img is loaded.
static void CallbackForIMG(IMG img, void *v)
{

  if (kVerbosity >= 2) {
    Printf("Started CallbackForIMG %s\n", IMG_Name(img).c_str());
  }
//  for(SYM sym = IMG_RegsymHead(img); SYM_Valid(sym); sym = SYM_Next(sym)) {
//    string name = SYM_Name(sym);
//    Printf("Sym: %s\n", name.c_str());
//  }

  // Check if we want to spend time searching this img for 
  // some particular functions.
  string img_name = IMG_Name(img);
  // save the addresses of *all* routines in a map.
  if (!routine_address_map) {
    routine_address_map  = new map<uintptr_t, Routine*>;
  }
  for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
    for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
      string rtn_name = RTN_Name(rtn);
      Routine *routine = new Routine;
      routine->rtn_name = rtn_name;
      (*routine_address_map)[RTN_Address(rtn)] = routine;
      MaybeInstrumentOneRoutine(img, rtn);
    }
  }
  // Don't as me why, but malloc on Linux requires special attention.
  RTN malloc_rtn = RTN_FindByName(img, "malloc");
  if (RTN_Valid(malloc_rtn)) {
    Printf("zzz\n");
    INSERT_FN_HELPER(IPOINT_BEFORE, "malloc", malloc_rtn, Before_malloc,
                     IARG_FUNCARG_ENTRYPOINT_VALUE, 0);
    INSERT_FN_HELPER(IPOINT_AFTER, "malloc", malloc_rtn, After_malloc,
                     IARG_FUNCRET_EXITPOINT_VALUE);
  }


}

//--------- Init/Fini ---------- {{{1
static void Init() {
  Printf("%s\n", __FUNCTION__);
}

static void CallbackForFini(INT32 code, void *v) {
  Printf("%s\n", __FUNCTION__);
  Printf("** dyn read/write: %'lld %'lld\n", dyn_read_count, dyn_write_count);
  Printf("** n_created_threads: %d\n", n_created_threads);
}


//--------- Main -------------------------- {{{1
int main(INT32 argc, CHAR **argv) {
  PIN_Init(argc, argv);
  PIN_InitSymbols();

  Init();

  PIN_AddThreadStartFunction(CallbackForThreadStart, 0);
  PIN_AddThreadFiniFunction(CallbacForThreadFini, 0);
  PIN_AddFiniFunction(CallbackForFini, 0);
  IMG_AddInstrumentFunction(CallbackForIMG, 0);
  TRACE_AddInstrumentFunction(CallbackForTRACE, 0);

  PIN_StartProgram();
  return 0;
}



//--------- Questions about PIN -------------------------- {{{1
/* Questions about PIN:
  
  - Am I allowed to call pthread_create() in the pin tool?  -- **NO**
  - How to Instrument thread create/join events in parent 
  (other than intercepting pthread_create/join*)
  - Names (e.g. pthread_create@... __pthread_mutex_unlock)
  - How to get name of a global var by it's address?
  - How to get stack pointer at thread creation? 
  - How to get a stack trace (other than intercepting calls, entries, exits) 
  - assert with full stack trace?
  */
// end. {{{1
// vim:shiftwidth=2:softtabstop=2:expandtab
