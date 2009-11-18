/*
  This file is part of ThreadSanitizer, a dynamic data race detector
  based on Valgrind.

  Copyright (C) 2008-2009 Google Inc
     opensource@google.com
  Copyright (C) 2007-2008 OpenWorks LLP
      info@open-works.co.uk

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
// Parts of the code in this file are taken from Helgrind,
// a data race detector written by Julian Seward.



#ifdef TS_INSTR_VALGRIND_HERE

#include "ts_valgrind.h"
#include "valgrind.h"
#include "ts_valgrind_client_requests.h"
#include "thread_sanitizer.h"


//---------------------- C++ malloc support -------------- {{{1
class MallocCostCenterStack {
 public:
  void Push(const char *cc) {
    DCHECK(size_ < kMaxMallocStackSize);
    DCHECK(cc);
    malloc_cost_centers_[size_++] = cc;
  }
  void Pop() {
    DCHECK(size_ > 0);
    size_--;
  }
  const char *Top() {
    return size_ ? malloc_cost_centers_[size_ - 1] : "default_cc";
  }
 private:
  static const int kMaxMallocStackSize = 100;
  int size_;
  const char *malloc_cost_centers_[kMaxMallocStackSize];
};

// Not thread-safe. Need to make it thread-local once we are multi-threaded.
static MallocCostCenterStack g_malloc_stack;

void PushMallocCostCenter(const char *cc) { g_malloc_stack.Push(cc); }
void PopMallocCostCenter() { g_malloc_stack.Pop(); }


void *operator new (size_t size) {
  return VG_(malloc)((HChar*)g_malloc_stack.Top(), size);
}
void *operator new [](size_t size) {
  return VG_(malloc)((HChar*)g_malloc_stack.Top(), size);
}
void operator delete (void *p) {
  VG_(free)(p);
}
void operator delete [](void *p) {
  VG_(free)(p);
}



//---------------------- Utils ------------------- {{{1
extern "C" void * memmove(void *a, const void *b, size_t size) {
  return VG_(memmove)(a,b, size);
}

extern "C" int memcmp(const void *a, const void *b, size_t c) {
  return VG_(memcmp)(a,b,c);
}

// TODO: make this rtn public
extern "C" {
  Bool VG_(get_fnname_no_cxx_demangle) ( Addr a, Char* buf, Int nbuf );
}


const int kBuffSize = 1024 * 10 - 1;
// not thread-safe.
static char g_buff1[kBuffSize+1];
static char g_buff2[kBuffSize+1];

string PcToRtnName(uintptr_t pc, bool demangle) {
  if (demangle) {
    if(VG_(get_fnname)(pc, (Char*)g_buff1, kBuffSize)) {
      return g_buff1;
    }
  } else {
    if(VG_(get_fnname_no_cxx_demangle)(pc, (Char*)g_buff1, kBuffSize)) {
      return g_buff1;
    }
  }
  return "???";
}

void PcToStrings(uintptr_t pc, bool demangle,
                string *img_name, string *rtn_name,
                string *file_name, int *line_no) {
  const int kBuffSize = 1024 * 10 - 1;
  Bool has_dirname = False;

  if (VG_(get_filename_linenum)
      (pc, (Char*)g_buff1, kBuffSize, (Char*)g_buff2, kBuffSize,
       &has_dirname, (UInt*)line_no) &&
      has_dirname) {
    *file_name = string(g_buff2) + "/" + g_buff1;
  } else {
    VG_(get_linenum)(pc, (UInt *)line_no);
    if (VG_(get_filename)(pc, (Char*)g_buff1, kBuffSize)) {
      *file_name = g_buff1;
    }
  }

  *rtn_name = PcToRtnName(pc, demangle);

  if (VG_(get_objname)(pc, (Char*)g_buff1, kBuffSize)) {
    *img_name = g_buff1;
  }
}



string Demangle(const char *str) {
  return str;
}

extern "C"
size_t strlen(const char *s) {
  return VG_(strlen)((const Char*)s);
}

static inline uintptr_t GetVgPc(ThreadId vg_tid) {
  return (uintptr_t)VG_(get_IP)(vg_tid);
}

uintptr_t GetPcOfCurrentThread() {
  return GetVgPc(GetVgTid());
}

struct CallStackRecord {
  Addr pc;
  Addr sp;
};

struct ValgrindThread {
  int32_t zero_based_uniq_tid;
  vector<CallStackRecord> call_stack;

  int ignore_accesses;
  int ignore_sync;

  ValgrindThread()
    : zero_based_uniq_tid(-1),
      ignore_accesses(0),
      ignore_sync(0) {
  }
};


// Array of VG_N_THREADS
static ValgrindThread *g_valgrind_threads = 0;

// maintains a uniq thread id (first thread will have id=0)
static int32_t g_uniq_thread_id_counter = 0;

static int32_t VgTidToTsTid(ThreadId vg_tid) {
  DCHECK(vg_tid < VG_N_THREADS);
  DCHECK(vg_tid >= 1);
  DCHECK(g_valgrind_threads);
  DCHECK(g_valgrind_threads[vg_tid].zero_based_uniq_tid >= 0);
  return g_valgrind_threads[vg_tid].zero_based_uniq_tid;
}


static vector<string> *g_command_line_options = 0;
static void InitCommandLineOptions() {
  if (g_command_line_options == NULL) {
    g_command_line_options = new vector<string>;
  }
}

Bool ts_process_cmd_line_option (Char* arg) {
  InitCommandLineOptions();
  g_command_line_options->push_back((char*)arg);
  return True;
}

void ts_print_usage (void) {
  ThreadSanitizerPrintUsage();
}

void ts_print_debug_usage(void) {
  Printf("TODO\n");
}


void evh__die_mem ( Addr a, SizeT len ) {
}


void ts_post_clo_init(void) {
  G_flags = new FLAGS;
  InitCommandLineOptions();
  ThreadSanitizerParseFlags(g_command_line_options);
  if (G_flags->html) {
    Report("<pre>\n"
           "<br id=race0>"
           "<a href=\"#race1\">Go to first race report</a>\n");
  }
  Report("ThreadSanitizerValgrind: "
         "pure-happens-before=%s fast-mode=%s ignore-in-dtor=%s\n",
         G_flags->pure_happens_before ? "yes" : "no",
         G_flags->fast_mode ? "yes" : "no",
         G_flags->ignore_in_dtor ? "yes" : "no");
  if (G_flags->max_mem_in_mb) {
    Report("INFO: ThreadSanitizer memory limit: %dMB\n",
           (int)G_flags->max_mem_in_mb);
  }
  ThreadSanitizerInit();

  g_valgrind_threads = new ValgrindThread[VG_N_THREADS];
}

static inline void Put(EventType type, int32_t tid, uintptr_t pc,
                       uintptr_t a, uintptr_t info) {
  if (DEBUG_MODE && G_flags->dry_run >= 1) return;
  Event event(type, tid, pc, a, info);
  ThreadSanitizerHandleOneEvent(&event);
}



/* CALLED FROM GENERATED CODE */
VG_REGPARM(2)
void evh__new_frame ( Addr sp_post_call_insn,
                      Addr pc_post_call_insn) {
  ThreadId vg_tid = GetVgTid();
  CallStackRecord record;
  record.pc = pc_post_call_insn;
  record.sp = sp_post_call_insn;
  g_valgrind_threads[vg_tid].call_stack.push_back(record);
  uintptr_t call_pc = GetVgPc(vg_tid);

  ThreadSanitizerHandleRtnCall(VgTidToTsTid(vg_tid), call_pc, record.pc);


  if (G_flags->verbosity >= 2) {
    Printf("T%d: >>: %s\n", VgTidToTsTid(vg_tid),
           PcToRtnNameAndFilePos(record.pc).c_str());
  }
}

static INLINE void evh__new_mem_stack_helper ( Addr a, SizeT len ) {
  ThreadId vg_tid = GetVgTid();
  if (!g_valgrind_threads[vg_tid].ignore_accesses) {
    // avoid stack updates when ignore is on.
    // TODO: is that right?
    int32_t ts_tid = VgTidToTsTid(vg_tid);
    ThreadSanitizerHandleStackMemChange(ts_tid, a, len, true);
  }
}

static void evh__new_mem_stack ( Addr a, SizeT len ) {
  evh__new_mem_stack_helper(a, len);
}
VG_REGPARM(1)
static void evh__new_mem_stack_8 ( Addr a) {
  evh__new_mem_stack_helper(a, 8);
}
VG_REGPARM(1)
static void evh__new_mem_stack_16 ( Addr a) {
  evh__new_mem_stack_helper(a, 16);
}
VG_REGPARM(1)
static void evh__new_mem_stack_32 ( Addr a) {
  evh__new_mem_stack_helper(a, 32);
}



static INLINE void evh__die_mem_stack_helper ( Addr a, SizeT len ) {
  ThreadId vg_tid = GetVgTid();
  int32_t ts_tid = VgTidToTsTid(vg_tid);
  vector<CallStackRecord> &call_stack = g_valgrind_threads[vg_tid].call_stack;
  while (!call_stack.empty()) {
    CallStackRecord &record = call_stack.back();
    Addr cur_top = record.sp;
    if (a < cur_top) break;
    call_stack.pop_back();
    ThreadSanitizerHandleRtnExit(ts_tid);
    // Put(RTN_EXIT, ts_tid, 0, 0, 0);
    if (G_flags->verbosity >= 2) {
      Printf("T%d: <<\n", ts_tid);
    }
    break;
  }
  if (G_flags->verbosity >= 2) {
    // Printf("T%d: -sp: %p => %p (%ld)\n", ts_tid, a, a + len, len);
  }
  if (!g_valgrind_threads[vg_tid].ignore_accesses) {
    ThreadSanitizerHandleStackMemChange(ts_tid, a, len, false);
    // Put(STACK_MEM_DIE, ts_tid, 0, a, len);
  }
}
static void evh__die_mem_stack ( Addr a, SizeT len ) {
//  Printf("** %d\n", (int)len);
  evh__die_mem_stack_helper(a, len);
}
VG_REGPARM(1)
static void evh__die_mem_stack_8 ( Addr a) {
//  Printf("** 8\n");
  evh__die_mem_stack_helper(a, 8);
}
VG_REGPARM(1)
static void evh__die_mem_stack_16 ( Addr a ) {
//  Printf("** 16\n");
  evh__die_mem_stack_helper(a, 16);
}
VG_REGPARM(1)
static void evh__die_mem_stack_32 ( Addr a ) {
//  Printf("** 32\n");
  evh__die_mem_stack_helper(a, 32);
}


void ts_fini(Int exitcode) {
  ThreadSanitizerFini();
}


void evh__pre_thread_ll_create ( ThreadId parent, ThreadId child ) {
  tl_assert(parent != child);
  //  Printf("thread_create: %d->%d\n", parent, child);
  if (g_valgrind_threads[child].zero_based_uniq_tid != -1) {
    Printf("ThreadSanitizer WARNING: reusing TID %d w/o exiting thread\n",
           child);
  }
  g_valgrind_threads[child].zero_based_uniq_tid = g_uniq_thread_id_counter++;
  g_valgrind_threads[child].ignore_accesses = 0;
  // Printf("VG: T%d: VG_THR_START: parent=%d\n", VgTidToTsTid(child), VgTidToTsTid(parent));
  uintptr_t pc = GetVgPc(parent);
  Put(THR_START, VgTidToTsTid(child), pc, 0,
      parent > 0 ? VgTidToTsTid(parent) : 0);
}

void evh__pre_thread_first_insn(const ThreadId tid) {
  Put(THR_FIRST_INSN, VgTidToTsTid(tid), GetVgPc(tid), 0, 0);
}


void evh__pre_thread_ll_exit ( ThreadId quit_tid ) {
//  Printf("thread_exit: %d\n", quit_tid);
//  Printf("T%d quiting thread; stack size=%ld\n",
//         VgTidToTsTid(quit_tid),
//         (int)g_valgrind_threads[quit_tid].call_stack.size());
  Put(THR_END, VgTidToTsTid(quit_tid), 0, 0, 0);
  g_valgrind_threads[quit_tid].zero_based_uniq_tid = -1;
}


// Memory operation...
static INLINE void Mop(Addr a, bool is_w, SizeT size) {
  ThreadId vg_tid = GetVgTid();
  if (g_valgrind_threads[vg_tid].ignore_accesses) {
    //static int counter;
    //counter++;
    //if ((counter % 1024) == 0)
    //  Printf("ignore: %s\n", PcToRtnNameAndFilePos(pc).c_str());
    return;
  }
  ThreadSanitizerHandleMemoryAccess(VgTidToTsTid(vg_tid), a, size, is_w);
}


VG_REGPARM(1) void evh__mem_help_write_1(Addr a) { Mop(a, true, 1); }
VG_REGPARM(1) void evh__mem_help_write_2(Addr a) { Mop(a, true, 2); }
VG_REGPARM(1) void evh__mem_help_write_4(Addr a) { Mop(a, true, 4); }
VG_REGPARM(1) void evh__mem_help_write_8(Addr a) { Mop(a, true, 8); }
VG_REGPARM(1) void evh__mem_help_read_1(Addr a) { Mop(a, false, 1); }
VG_REGPARM(1) void evh__mem_help_read_2(Addr a) { Mop(a, false, 2); }
VG_REGPARM(1) void evh__mem_help_read_4(Addr a) { Mop(a, false, 4); }
VG_REGPARM(1) void evh__mem_help_read_8(Addr a) { Mop(a, false, 8); }
VG_REGPARM(2)
  void evh__mem_help_write_N(Addr a, SizeT size) { Mop(a, true, size); }
VG_REGPARM(2)
  void evh__mem_help_read_N(Addr a, SizeT size) { Mop(a, false, size); }


  extern "C" void VG_(show_all_errors)();

Bool ts_handle_client_request(ThreadId vg_tid, UWord* args, UWord* ret) {
  if (!VG_IS_TOOL_USERREQ('T', 'S', args[0]))
    return False;
  *ret = 0;
  uintptr_t pc = GetVgPc(vg_tid);
  int32_t ts_tid = VgTidToTsTid(vg_tid);
  switch (args[0]) {
    case TSREQ_SET_MY_PTHREAD_T:
      Put(THR_SET_PTID, ts_tid, pc, args[1], 0);
      break;
    case TSREQ_PTHREAD_JOIN_POST:
      // TODO: get rid of THR_JOIN_BEFORE
      Put(THR_JOIN_BEFORE, ts_tid, pc, args[1], 0);
      Put(THR_JOIN_AFTER, ts_tid, pc, 0, 0);
      break;
    case TSREQ_CLEAN_MEMORY:
      Put(MALLOC, ts_tid, pc, /*ptr=*/args[1], /*size=*/args[2]);
      break;
    case TSREQ_MAIN_IN:
      g_has_entered_main = true;
      // Report("INFO: Entred main(); argc=%d\n", (int)args[1]);
      break;
    case TSREQ_MAIN_OUT:
      g_has_exited_main = true;
      if (G_flags->exit_after_main) {
        Report("INFO: Exited main(); ret=%d\n", (int)args[1]);
        VG_(show_all_errors)();
        ThreadSanitizerFini();
        exit((int)args[1]);
      }
      break;
    case TSREQ_MALLOC:
      // Printf("Malloc: %p %ld\n", args[1], args[2]);
      Put(MALLOC, ts_tid, pc, /*ptr=*/args[1], /*size=*/args[2]);
      break;
    case TSREQ_FREE:
      // Printf("Free: %p\n", args[1]);
      Put(FREE, ts_tid, pc, /*ptr=*/args[1], 0);
      break;
    case TSREQ_BENIGN_RACE:
      Put(EXPECT_RACE, ts_tid, /*descr=*/args[2],
          /*p=*/args[1], /*is_benign=*/1);
      break;
    case TSREQ_EXPECT_RACE:
      Put(EXPECT_RACE, ts_tid, /*descr=*/args[2],
          /*p=*/args[1], /*is_benign*/0);
      break;
    case TSREQ_PCQ_CREATE:
      Put(PCQ_CREATE, ts_tid, pc, /*pcq=*/args[1], 0);
      break;
    case TSREQ_PCQ_DESTROY:
      Put(PCQ_DESTROY, ts_tid, pc, /*pcq=*/args[1], 0);
      break;
    case TSREQ_PCQ_PUT:
      Put(PCQ_PUT, ts_tid, pc, /*pcq=*/args[1], 0);
      break;
    case TSREQ_PCQ_GET:
      Put(PCQ_GET, ts_tid, pc, /*pcq=*/args[1], 0);
      break;
    case TSREQ_TRACE_MEM:
      Put(TRACE_MEM, ts_tid, pc, /*mem=*/args[1], 0);
      break;
    case TSREQ_MUTEX_IS_USED_AS_CONDVAR:
      Put(HB_LOCK, ts_tid, pc, /*lock=*/args[1], 0);
      break;
    case TSREQ_IGNORE_READS_BEGIN:
      Put(IGNORE_READS_BEG, ts_tid, pc, 0, 0);
      break;
    case TSREQ_IGNORE_READS_END:
      Put(IGNORE_READS_END, ts_tid, pc, 0, 0);
      break;
    case TSREQ_IGNORE_WRITES_BEGIN:
      Put(IGNORE_WRITES_BEG, ts_tid, pc, 0, 0);
      break;
    case TSREQ_IGNORE_WRITES_END:
      Put(IGNORE_WRITES_END, ts_tid, pc, 0, 0);
      break;
    case TSREQ_SET_THREAD_NAME:
      Put(SET_THREAD_NAME, ts_tid, pc, /*name=*/args[1], 0);
      break;
    case TSREQ_SET_LOCK_NAME:
      Put(SET_LOCK_NAME, ts_tid, pc, /*lock=*/args[1], /*name=*/args[2]);
      break;
    case TSREQ_IGNORE_ALL_ACCESSES_BEGIN:
      g_valgrind_threads[vg_tid].ignore_accesses++;
      break;
    case TSREQ_IGNORE_ALL_ACCESSES_END:
      g_valgrind_threads[vg_tid].ignore_accesses--;
      CHECK(g_valgrind_threads[vg_tid].ignore_accesses >= 0);
      break;
    case TSREQ_IGNORE_ALL_SYNC_BEGIN:
      g_valgrind_threads[vg_tid].ignore_sync++;
      break;
    case TSREQ_IGNORE_ALL_SYNC_END:
      g_valgrind_threads[vg_tid].ignore_sync--;
      CHECK(g_valgrind_threads[vg_tid].ignore_sync >= 0);
      break;
    case TSREQ_PUBLISH_MEMORY_RANGE:
      Put(PUBLISH_RANGE, ts_tid, pc, /*mem=*/args[1], /*size=*/args[2]);
      break;
    case TSREQ_UNPUBLISH_MEMORY_RANGE:
      Put(UNPUBLISH_RANGE, ts_tid, pc, /*mem=*/args[1], /*size=*/args[2]);
      break;
    case TSREQ_PRINT_MEMORY_USAGE:
    case TSREQ_PRINT_STATS:
    case TSREQ_RESET_STATS:
    case TSREQ_PTH_API_ERROR:
      break;
    case TSREQ_PTHREAD_COND_SIGNAL_PRE:
    case TSREQ_PTHREAD_COND_BROADCAST_PRE:
      Put(SIGNAL, ts_tid, pc, /*cv=*/args[1], 0);
      break;
    case TSREQ_PTHREAD_COND_WAIT_PRE:
      Put(WAIT_BEFORE, ts_tid, pc, /*cv=*/args[1], /*lock=*/args[2]);
      break;
    case TSREQ_PTHREAD_COND_WAIT_POST:
      Put(WAIT_AFTER, ts_tid, pc, 0, 0);
      break;
    case TSREQ_PTHREAD_COND_TWAIT_POST:
      Put(TWAIT_AFTER, ts_tid, pc, 0, 0);
      break;
    case TSREQ_PTHREAD_RWLOCK_CREATE_POST:
      Put(LOCK_CREATE, ts_tid, pc, /*lock=*/args[1], 0);
      break;
    case TSREQ_PTHREAD_RWLOCK_DESTROY_PRE:
      Put(LOCK_DESTROY, ts_tid, pc, /*lock=*/args[1], 0);
      break;
    case TSREQ_PTHREAD_RWLOCK_LOCK_PRE:
      break;
    case TSREQ_PTHREAD_RWLOCK_LOCK_POST:
      if (g_valgrind_threads[vg_tid].ignore_sync) break;
      Put(LOCK_BEFORE, ts_tid, pc, /*lock=*/args[1], 0);
      Put(args[2] ? WRITER_LOCK : READER_LOCK, ts_tid, pc, /*lock=*/args[1], 0);
      break;
    case TSREQ_PTHREAD_RWLOCK_UNLOCK_PRE:
      if (g_valgrind_threads[vg_tid].ignore_sync) break;
      Put(UNLOCK, ts_tid, pc, /*lock=*/args[1], 0);
      break;
    case TSREQ_PTHREAD_SPIN_LOCK_INIT_OR_UNLOCK:
      Put(UNLOCK_OR_INIT, ts_tid, pc, /*lock=*/args[1], 0);
      break;
    case TSREQ_PTHREAD_RWLOCK_UNLOCK_POST:
      break;

    case TSREQ_POSIX_SEM_INIT_POST:
    case TSREQ_POSIX_SEM_DESTROY_PRE:
      break;
    case TSREQ_POSIX_SEM_POST_PRE:
      Put(SIGNAL, ts_tid, pc, /*sem=*/args[1], 0);
      break;
    case TSREQ_POSIX_SEM_WAIT_POST:
      Put(WAIT_BEFORE, ts_tid, pc, /*sem=*/args[1], 0);
      Put(WAIT_AFTER, ts_tid, pc, 0, 0);
      break;

    case TSREQ_GET_MY_SEGMENT:
      break;
    case TSREQ_GET_THREAD_ID:
      *ret = ts_tid;
      break;
    case TSREQ_GET_VG_THREAD_ID:
      *ret = vg_tid;
      break;
    case TSREQ_GET_SEGMENT_ID:
      break;
    default: CHECK(0);
  }
  return True;
}



VG_REGPARM(0) static void evh__create_new_segment_for_history(void) {
  ThreadId vg_tid = GetVgTid();
  uintptr_t pc = GetVgPc(vg_tid);
  if (g_valgrind_threads[vg_tid].ignore_accesses) return;
  ThreadSanitizerEnterSblock(VgTidToTsTid(vg_tid), pc);
  // Put(SBLOCK_ENTER, VgTidToTsTid(vg_tid), pc, 0, 0);
}




// ---------------------------- Instrumentation ---------------------------{{{1

static IRTemp gen_Get_SP ( IRSB*           bbOut,
                           VexGuestLayout* layout,
                           Int             hWordTy_szB )
{
  IRExpr* sp_expr;
  IRTemp  sp_temp;
  IRType  sp_type;
  /* This in effect forces the host and guest word sizes to be the
     same. */
  tl_assert(hWordTy_szB == layout->sizeof_SP);
  sp_type = layout->sizeof_SP == 8 ? Ity_I64 : Ity_I32;
  sp_expr = IRExpr_Get( layout->offset_SP, sp_type );
  sp_temp = newIRTemp( bbOut->tyenv, sp_type );
  addStmtToIRSB( bbOut, IRStmt_WrTmp( sp_temp, sp_expr ) );
  return sp_temp;
}


static void ts_instrument_create_new_segment_for_history(IRSB *bbOut) {
   HChar*   hName    = (HChar*)"evh__create_new_segment_for_history";
   IRDirty* di = unsafeIRDirty_0_N( 0,
                           hName,
                           VG_(fnptr_to_fnentry)((void*)evh__create_new_segment_for_history),
                           mkIRExprVec_0());
   addStmtToIRSB( bbOut, IRStmt_Dirty(di));
}



static void ts_instrument_final_jump (
                                /*MOD*/IRSB* sbOut,
                                IRExpr* next,
                                IRJumpKind jumpkind,
                                VexGuestLayout* layout,
                                IRType gWordTy, IRType hWordTy ) {

  if (jumpkind != Ijk_Call)  return;
  {
    // Assumes x86 or amd64
    IRTemp sp_post_call_insn
        = gen_Get_SP( sbOut, layout, sizeofIRType(hWordTy) );
    IRExpr **args = mkIRExprVec_2(
        IRExpr_RdTmp(sp_post_call_insn),
        next
        );
    IRDirty* di = unsafeIRDirty_0_N(
        2/*regparms*/,
        (char*)"evh__new_frame",
        VG_(fnptr_to_fnentry)((void*) &evh__new_frame ),
        args );
    addStmtToIRSB( sbOut, IRStmt_Dirty(di) );
  }
}


static void instrument_mem_access ( IRSB*   bbOut,
                                    IRExpr* addr,
                                    Int     szB,
                                    Bool    isStore,
                                    Int     hWordTy_szB )
{
   IRType   tyAddr   = Ity_INVALID;
   const HChar*   hName    = NULL;
   void*    hAddr    = NULL;
   Int      regparms = 0;
   IRExpr** argv     = NULL;
   IRDirty* di       = NULL;

   tl_assert(isIRAtom(addr));
   tl_assert(hWordTy_szB == 4 || hWordTy_szB == 8);

   tyAddr = typeOfIRExpr( bbOut->tyenv, addr );
   tl_assert(tyAddr == Ity_I32 || tyAddr == Ity_I64);

   /* So the effective address is in 'addr' now. */
   regparms = 1; // unless stated otherwise
   if (isStore) {
      switch (szB) {
         case 1:
            hName = "evh__mem_help_write_1";
            hAddr = (void*)&evh__mem_help_write_1;
            argv = mkIRExprVec_1( addr );
            break;
         case 2:
            hName = "evh__mem_help_write_2";
            hAddr = (void*)&evh__mem_help_write_2;
            argv = mkIRExprVec_1( addr );
            break;
         case 4:
            hName = "evh__mem_help_write_4";
            hAddr = (void*)&evh__mem_help_write_4;
            argv = mkIRExprVec_1( addr );
            break;
         case 8:
            hName = "evh__mem_help_write_8";
            hAddr = (void*)&evh__mem_help_write_8;
            argv = mkIRExprVec_1( addr );
            break;
         default:
            tl_assert(szB > 8 && szB <= 512); /* stay sane */
            regparms = 2;
            hName = "evh__mem_help_write_N";
            hAddr = (void*)&evh__mem_help_write_N;
            argv = mkIRExprVec_2( addr, mkIRExpr_HWord( szB ));
            break;
      }
   } else {
      switch (szB) {
         case 1:
            hName = "evh__mem_help_read_1";
            hAddr = (void*)&evh__mem_help_read_1;
            argv = mkIRExprVec_1( addr );
            break;
         case 2:
            hName = "evh__mem_help_read_2";
            hAddr = (void*)&evh__mem_help_read_2;
            argv = mkIRExprVec_1( addr );
            break;
         case 4:
            hName = "evh__mem_help_read_4";
            hAddr = (void*)&evh__mem_help_read_4;
            argv = mkIRExprVec_1( addr );
            break;
         case 8:
            hName = "evh__mem_help_read_8";
            hAddr = (void*)&evh__mem_help_read_8;
            argv = mkIRExprVec_1( addr );
            break;
         default:
            tl_assert(szB > 8 && szB <= 512); /* stay sane */
            regparms = 2;
            hName = "evh__mem_help_read_N";
            hAddr = (void*)&evh__mem_help_read_N;
            argv = mkIRExprVec_2( addr, mkIRExpr_HWord( szB ));
            break;
      }
   }

   /* Add the helper. */
   tl_assert(hName);
   tl_assert(hAddr);
   tl_assert(argv);
   di = unsafeIRDirty_0_N( regparms,
                           (HChar*)hName, VG_(fnptr_to_fnentry)( hAddr ),
                           argv );
   addStmtToIRSB( bbOut, IRStmt_Dirty(di) );
}


void instrument_statement ( IRStmt* st, IRSB* bbIn, IRSB* bbOut, IRType hWordTy ) {
  switch (st->tag) {
    case Ist_NoOp:
    case Ist_AbiHint:
    case Ist_Put:
    case Ist_PutI:
    case Ist_IMark:
    case Ist_Exit:
      /* None of these can contain any memory references. */
      break;

    case Ist_MBE:
      //instrument_memory_bus_event( bbOut, st->Ist.MBE.event );
      switch (st->Ist.MBE.event) {
        case Imbe_Fence:
          break; /* not interesting */
        default:
          ppIRStmt(st);
          tl_assert(0);
      }
      break;

    case Ist_CAS:
      break;

    case Ist_Store:
      instrument_mem_access(
        bbOut,
        st->Ist.Store.addr,
        sizeofIRType(typeOfIRExpr(bbIn->tyenv, st->Ist.Store.data)),
        True/*isStore*/,
        sizeofIRType(hWordTy)
      );
      break;

    case Ist_WrTmp: {
      IRExpr* data = st->Ist.WrTmp.data;
      if (data->tag == Iex_Load) {
        instrument_mem_access(
            bbOut,
            data->Iex.Load.addr,
            sizeofIRType(data->Iex.Load.ty),
            False/*!isStore*/,
            sizeofIRType(hWordTy)
            );
      }
      break;
    }

    case Ist_Dirty: {
      Int      dataSize;
      IRDirty* d = st->Ist.Dirty.details;
      if (d->mFx != Ifx_None) {
        /* This dirty helper accesses memory.  Collect the
           details. */
        tl_assert(d->mAddr != NULL);
        tl_assert(d->mSize != 0);
        dataSize = d->mSize;
        if (d->mFx == Ifx_Read || d->mFx == Ifx_Modify) {
          instrument_mem_access(
            bbOut, d->mAddr, dataSize, False/*!isStore*/,
            sizeofIRType(hWordTy)
          );
        }
        if (d->mFx == Ifx_Write || d->mFx == Ifx_Modify) {
          instrument_mem_access(
            bbOut, d->mAddr, dataSize, True/*isStore*/,
            sizeofIRType(hWordTy)
          );
        }
      } else {
        tl_assert(d->mAddr == NULL);
        tl_assert(d->mSize == 0);
      }
      break;
    }

    default:
      ppIRStmt(st);
      tl_assert(0);
  } /* switch (st->tag) */
}


static IRSB* ts_instrument ( VgCallbackClosure* closure,
                             IRSB* bbIn,
                             VexGuestLayout* layout,
                             VexGuestExtents* vge,
                             IRType gWordTy, IRType hWordTy ) {
  if (G_flags->dry_run >= 2) return bbIn;
  Int   i;
  IRSB* bbOut;

  bool instrument_memory =
      ThreadSanitizerWantToInstrumentSblock(closure->nraddr);


  if (gWordTy != hWordTy) {
    /* We don't currently support this case. */
    VG_(tool_panic)((Char*)"host/guest word size mismatch");
  }

  /* Set up BB */
  bbOut           = emptyIRSB();
  bbOut->tyenv    = deepCopyIRTypeEnv(bbIn->tyenv);
  bbOut->next     = deepCopyIRExpr(bbIn->next);
  bbOut->jumpkind = bbIn->jumpkind;

  // Copy verbatim any IR preamble preceding the first IMark
  i = 0;
  while (i < bbIn->stmts_used && bbIn->stmts[i]->tag != Ist_IMark) {
    addStmtToIRSB( bbOut, bbIn->stmts[i] );
    i++;
  }
  int first = i;
  for (/*use current i*/; i < bbIn->stmts_used; i++) {
    IRStmt* st = bbIn->stmts[i];
    tl_assert(st);
    tl_assert(isFlatIRStmt(st));

    if (instrument_memory) {
      if (i == first && G_flags->keep_history >= 1) {
        ts_instrument_create_new_segment_for_history(bbOut);
      }
      instrument_statement(st, bbIn, bbOut, hWordTy);
    }

    addStmtToIRSB( bbOut, st );
  } /* iterate over bbIn->stmts */

  ts_instrument_final_jump(bbOut, bbIn->next, bbIn->jumpkind, layout, gWordTy, hWordTy);

  return bbOut;
}

static Bool eq_Error ( VgRes not_used, Error* e1, Error* e2 ) {
  return True;
}

static void before_pp_Error ( Error* err ) { }

static void pp_Error ( Error* err ) {
  void *extra = VG_(get_error_extra)(err);
  ThreadSanitizerPrintReport((ThreadSanitizerReport*)extra);
}

static UInt update_extra ( Error* err ) { return 0; }

static Bool recognised_suppression ( Char* name, Supp *su )
{
#  define TRY(_name,_xskind)                   \
      if (0 == VG_(strcmp)(name, (Char*)(_name))) {   \
         VG_(set_supp_kind)(su, (_xskind));    \
         return True;                          \
      }
   TRY("Race",           XS_Race);
   TRY("UnlockForeign",  XS_UnlockForeign);
   TRY("UnlockNonLocked",  XS_UnlockNonLocked);
   TRY("InvalidLock",    XS_InvalidLock);
   return False;
#  undef TRY
}


static Bool read_extra_suppression_info(Int fd, Char** buf, SizeT* nBuf, Supp* su) {
  return True;
}
static Bool error_matches_suppression(Error* err, Supp* su) {
  switch (VG_(get_supp_kind)(su)) {
    case XS_Race:           return VG_(get_error_kind)(err) == XS_Race;
    case XS_UnlockForeign:  return VG_(get_error_kind)(err) == XS_UnlockForeign;
    case XS_UnlockNonLocked:
                          return VG_(get_error_kind)(err) == XS_UnlockNonLocked;
    case XS_InvalidLock:    return VG_(get_error_kind)(err) == XS_InvalidLock;
  }
  return False;
}
static Bool get_extra_suppression_info(Error* err,
                                       /*OUT*/Char* buf, Int nBuf) {
  return False;
}
static Char* get_error_name ( Error* err )
{
   switch (VG_(get_error_kind)(err)) {
      case XS_Race:            return (Char*)"Race";
      case XS_UnlockForeign:   return (Char*)"UnlockForeign";
      case XS_UnlockNonLocked: return (Char*)"UnlockNonLocked";
      case XS_InvalidLock:     return (Char*)"InvalidLock";
   }
   tl_assert(0);
   return NULL;
}


extern "C"
void ts_pre_clo_init(void) {
  VG_(details_name)            ((Char*)"ThreadSanitizer");
  VG_(details_version)         ((Char*)NULL);
  VG_(details_description)     ((Char*)"a data race detector");
  VG_(details_copyright_author)(
      (Char*)"Copyright (C) 2008-2009, and GNU GPL'd, by Google Inc.");
  VG_(details_bug_reports_to)  ((Char*)"TODO(kcc)");

  VG_(basic_tool_funcs)        (ts_post_clo_init,
                                ts_instrument,
                                ts_fini);


  VG_(needs_client_requests)     (ts_handle_client_request);

  VG_(needs_command_line_options)(ts_process_cmd_line_option,
                                  ts_print_usage,
                                  ts_print_debug_usage);

  VG_(needs_tool_errors)         (eq_Error,
                                  before_pp_Error,
                                  pp_Error,
                                  False, //show TIDs for errors
                                  update_extra,
                                  recognised_suppression,
                                  read_extra_suppression_info,
                                  error_matches_suppression,
                                  get_error_name,
                                  get_extra_suppression_info);


//   VG_(needs_var_info)(); // optional

/*
   VG_(needs_core_errors)         ();


   // FIXME?
   //VG_(needs_sanity_checks)       (hg_cheap_sanity_check,
   //                                hg_expensive_sanity_check);

   VG_(needs_malloc_replacement)  (hg_cli__malloc,
                                   hg_cli____builtin_new,
                                   hg_cli____builtin_vec_new,
                                   hg_cli__memalign,
                                   hg_cli__calloc,
                                   hg_cli__free,
                                   hg_cli____builtin_delete,
                                   hg_cli____builtin_vec_delete,
                                   hg_cli__realloc,
                                   HG_CLI__MALLOC_REDZONE_SZB );



   // FIXME: surely this isn't thread-aware
   VG_(track_copy_mem_remap)      ( shadow_mem_copy_range );

   VG_(track_change_mem_mprotect) ( evh__set_perms );

   */

//   VG_(track_new_mem_startup)     ( evh__new_mem_w_perms );
//   VG_(track_new_mem_stack_signal)( evh__new_mem_w_tid );
//   VG_(track_new_mem_brk)         ( evh__new_mem_w_tid );
//   VG_(track_new_mem_mmap)        ( evh__new_mem_w_perms );

   VG_(track_new_mem_stack)       ( evh__new_mem_stack);
   VG_(track_new_mem_stack_8)       ( evh__new_mem_stack_8);
   VG_(track_new_mem_stack_16)       ( evh__new_mem_stack_16);
   VG_(track_new_mem_stack_32)       ( evh__new_mem_stack_32);


   VG_(track_die_mem_stack)       ( evh__die_mem_stack );
   VG_(track_die_mem_stack_8)     ( evh__die_mem_stack_8 );
   VG_(track_die_mem_stack_16)     ( evh__die_mem_stack_16 );
   VG_(track_die_mem_stack_32)     ( evh__die_mem_stack_32 );

   VG_(track_die_mem_stack_signal)( evh__die_mem );
   VG_(track_die_mem_brk)         ( evh__die_mem );
   VG_(track_die_mem_munmap)      ( evh__die_mem );

   /*
   // FIXME: what is this for?
   VG_(track_ban_mem_stack)       (NULL);

   VG_(track_pre_mem_read)        ( evh__pre_mem_read );
   VG_(track_pre_mem_read_asciiz) ( evh__pre_mem_read_asciiz );
   VG_(track_pre_mem_write)       ( evh__pre_mem_write );
   VG_(track_post_mem_write)      (NULL);

   /////////////////

   */
   VG_(track_pre_thread_ll_create)( evh__pre_thread_ll_create );
   VG_(track_pre_thread_first_insn)( evh__pre_thread_first_insn );
   VG_(track_pre_thread_ll_exit)  ( evh__pre_thread_ll_exit );

//
//   VG_(track_start_client_code)( evh__start_client_code );
//   VG_(track_stop_client_code)( evh__stop_client_code );

   VG_(clo_vex_control).iropt_unroll_thresh = 0;
   VG_(clo_vex_control).guest_chase_thresh = 0;
}


VG_DETERMINE_INTERFACE_VERSION(ts_pre_clo_init)

#endif //  TS_INSTR_VALGRIND_HERE
// {{{1 end
// vim:shiftwidth=2:softtabstop=2:expandtab
