/*
  This file is part of ThreadSanitizer, a dynamic data race detector 
  based on Valgrind.

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

// You can find the details on this tool at 
// http://code.google.com/p/data-race-test

#include "thread_sanitizer.h"
#include <stdarg.h>
//--------- Constants --------------- {{{1
// Segment ID (SID)      is in range [1, kMaxSID-1]
// Segment Set ID (SSID) is in range [-kMaxSID+1, -1]
// This is not a compile-time constant, but it can only be changed 
// at startup.
int kMaxSID = (1 << 23);

// Lock ID (LID)      is in range [1, kMaxLID-1]
// Lock Set ID (LSID) is in range [-kMaxLID+1, -1]
const int kMaxLID = (1 << 23);

// This is not a compile-time constant, but it can be changed only at startup.
int kSizeOfHistoryStackTrace = 10;

// Maximal number of segments in a SegmentSet. 
// If you change this constant, you also need to change several places 
// in SegmentSet code. 
const int kMaxSegmentSetSize = 4;

//--------- Globals --------------- {{{1

bool g_so_far_only_one_thread = false;
bool g_has_entered_main = false;
bool g_has_exited_main = false;

FLAGS *G_flags = NULL;

//--------- Utils --------------- {{{1
// Read the contents of a file to string. Valgrind version.
static string ReadFileToString(const string &file_name) {
  SysRes sres = VG_(open)((const Char*)file_name.c_str(), VKI_O_RDONLY, 0 );
  if (sres.isError) {
    Report("WARNING: can not open file %s\n", file_name.c_str());
    exit(1);
  }
  int fd = sres.res;
  char buff[257] = {0};
  int n_read;
  string res;
  while ((n_read = VG_(read)(fd, buff, sizeof(buff) - 1)) > 0) {
    buff[n_read] = 0;
    res += buff;
  }

  VG_(close)(fd);
  return res;
}

// Sets the contents of the file 'file_name' to 'str'.
static void OpenFileWriteStringAndClose(const string &file_name, 
                                        const string &str) {
  SysRes sres = VG_(open)((const Char*)file_name.c_str(), 
                          VKI_O_WRONLY|VKI_O_CREAT|VKI_O_TRUNC,
                          VKI_S_IRUSR|VKI_S_IWUSR);
  if (sres.isError) {
    Report("WARNING: can not open file %s\n", file_name.c_str());
    exit(1);
  }
  int fd = sres.res;
  VG_(write)(fd, str.c_str(), str.size());
  VG_(close)(fd);
}

//--------- Stats ------------------- {{{1
// Statistic counters for the entire tool.
struct Stats {
  Stats() {
    memset(this, 0, sizeof(*this));
  }

  void PrintStats() {
    PrintEventStats();
    Printf("   VTS: created small/big: %'ld / %'ld; deleted: %'ld; cloned: %'ld\n", 
           vts_create_small, vts_create_big, vts_delete, vts_clone);
    Printf("   vts_total_size  = %'ld; avg=%'ld\n",
           vts_total_size, vts_total_size / (vts_create_small + vts_create_big + 1));
    Printf("   n_seg_hb        = %'ld\n", n_seg_hb);
    Printf("   n_vts_hb        = %'ld\n", n_vts_hb);
    Printf("   n_vts_hb_cached = %'ld\n", n_vts_hb_cached);
    Printf("   memory access:\n"
           "     1: %'ld\n" 
           "     2: %'ld\n" 
           "     4: %'ld\n" 
           "     8: %'ld\n" 
           "     s: %'ld\n",
           n_access1, n_access2, n_access4, n_access8, n_access_slow);
    PrintStatsForCache();
//    Printf("   Mops:\n"
//           "    total  = %'ld\n"
//           "    unique = %'ld\n",
//           mops_total, mops_uniq);
    Printf("   Publish: set: %'ld; get: %'ld; clear: %'ld\n", 
           publish_set, publish_get, publish_clear);

    Printf("   PcTo: all: %'ld rtn: %'ld\n", pc_to_strings, pc_to_rtn_name);

    Printf("   StackTrace: create: %'ld; delete %'ld\n", 
           stack_trace_create, stack_trace_delete);

    Printf("   History segments: same: %'ld; reuse: %'ld; new: %'ld\n", 
           history_uses_same_segment, history_reuses_segment, 
           history_creates_new_segment);
    Printf("   Forget all history: %'ld\n", n_forgets);

    if (G_flags->fast_mode)
      Printf("   Fast mode: first time      : %'ld;\n"
             "              still in creator: %'ld;\n"
             "              transition      : %'ld;\n"
             "              mt              : %'ld;\n", 
             fast_mode_first_time, fast_mode_still_in_creator, 
             fast_mode_transition, fast_mode_mt);
    PrintStatsForSeg();
    PrintStatsForSS();
    PrintStatsForLS();
  }

  void PrintStatsForSS() {
    Printf("   SegmentSet: created: %'ld; reused: %'ld;" 
           " find: %'ld; recycle: %'ld\n", 
           ss_create, ss_reuse, ss_find, ss_recycle);
    Printf("        sizes: 2: %'ld; 3: %'ld; 4: %'ld; other: %'ld\n", 
           ss_size_2, ss_size_3, ss_size_4, ss_size_other);
  }
  void PrintStatsForCache() {
    Printf("   Cache:\n" 
           "    fast      = %'ld\n"
           "    new       = %'ld\n"
           "    delete    = %'ld\n"
           "    fetch     = %'ld\n"
           "    storage   = %'ld\n",
           cache_fast_get, cache_new_line, 
           cache_delete_empty_line, cache_fetch, 
           cache_max_storage_size);
  }

  void PrintStatsForSeg() {
    Printf("   Segment: created: %'ld; reused: %'ld\n", 
           seg_create, seg_reuse);
  }

  void PrintStatsForLS() {
    Printf("   LockSet add: 0: %'ld; 1 : %'ld; n : %'ld\n", 
           ls_add_to_empty, ls_add_to_singleton, ls_add_to_multi);
    Printf("   LockSet rem: 1: %'ld; n : %'ld\n", 
           ls_remove_from_singleton, ls_remove_from_multi);
    Printf("   LockSet cache: add : %'ld; rem : %'ld; fast: %'ld\n", 
           ls_add_cache_hit, ls_rem_cache_hit, ls_cache_fast);
  }

  void PrintEventStats() {
    uintptr_t total = 0;
    for (int i = 0; i < LAST_EVENT; i++) {
      if (events[i]) {
        Printf("  %25s: %'ld\n", Event::TypeString((EventType)i), 
               events[i]);
      }
      total += events[i];
    }
    Printf("  %25s: %'ld\n", "Total", total);
    const int n_sizes = sizeof(memory_access_sizes_) /
                        sizeof(memory_access_sizes_[0]);
    for (int i = 0; i < n_sizes; i++) {
      if (memory_access_sizes_[i]) {
        Printf("  mop[%d]: %'ld\n", i, memory_access_sizes_[i]);
      }
    }
  }


  uintptr_t memory_access_sizes_[18];
  uintptr_t events[LAST_EVENT];

  uintptr_t n_vts_hb;
  uintptr_t n_vts_hb_cached;
  uintptr_t n_seg_hb;

  uintptr_t ls_add_to_empty, ls_add_to_singleton, ls_add_to_multi,
            ls_remove_from_singleton, ls_remove_from_multi, 
            ls_add_cache_hit, ls_rem_cache_hit, 
            ls_cache_fast;

  uintptr_t n_access1, n_access2, n_access4, n_access8, n_access_slow; 

  uintptr_t cache_fast_get;
  uintptr_t cache_new_line;
  uintptr_t cache_delete_empty_line;
  uintptr_t cache_fetch;
  uintptr_t cache_max_storage_size;

  uintptr_t mops_total;
  uintptr_t mops_uniq;

  uintptr_t vts_create_big, vts_create_small, vts_clone, vts_delete, vts_total_size;

  uintptr_t ss_create, ss_reuse, ss_find, ss_recycle;
  uintptr_t ss_size_2, ss_size_3, ss_size_4, ss_size_other;

  uintptr_t seg_create, seg_reuse;

  uintptr_t publish_set, publish_get, publish_clear;

  uintptr_t fast_mode_first_time, fast_mode_still_in_creator, 
            fast_mode_transition, fast_mode_mt;

  uintptr_t pc_to_strings, pc_to_rtn_name;

  uintptr_t stack_trace_create, stack_trace_delete;

  uintptr_t history_uses_same_segment, history_creates_new_segment, history_reuses_segment;

  uintptr_t n_forgets;
};

static Stats *G_stats;

//--------- Util ----------------------------- {{{1
// Like Print(), but prepend each line with ==XXXXX==, 
// where XXXXX is the pid.
void Report(const char *format, ...) {
  int buff_size = 1024*16;
  char *buff = new char [buff_size];

  va_list args;

  string res;
  while (1) {
    va_start(args, format);
    int ret = vsnprintf(buff, buff_size, format, args);
    va_end(args);
    if (ret < buff_size) break;
    delete [] buff;
    buff_size *= 2;
    buff = new char[buff_size];
    // Printf("Resized buff: %d\n", buff_size);
  }

  char pid_buff[100];
  sprintf(pid_buff, "==%d== ", getpid());
  int len = strlen(buff);
  bool last_was_new_line = true;
  for (int i = 0; i < len; i++) {
    if (last_was_new_line) 
      res += pid_buff;
    last_was_new_line = (buff[i] == '\n');
    res += buff[i];
  }

  Printf("%s", res.c_str());
}

string PcToRtnNameWithStats(uintptr_t pc, bool demangle) {
  G_stats->pc_to_rtn_name++;
  return PcToRtnName(pc, demangle);
}


string PcToRtnNameAndFilePos(uintptr_t pc) {
  G_stats->pc_to_strings++;
  string img_name;
  string file_name;
  string rtn_name;
  int line_no = -1;
  PcToStrings(pc, G_flags->demangle, &img_name, &rtn_name, &file_name, &line_no);
  for (size_t i = 0; i < G_flags->file_prefix_to_cut.size(); i++) {
    string prefix_to_cut = G_flags->file_prefix_to_cut[i];
    size_t pos = file_name.find(prefix_to_cut);
    if (pos != string::npos) {
      file_name = file_name.substr(pos + prefix_to_cut.size());
    }
    if (file_name.find("./") == 0) {  // remove leading ./
      file_name = file_name.substr(2); 
    }
  }
  if (file_name == "") {
    return rtn_name + " " + img_name;
  }
  char buff[10];
  sprintf(buff, "%d", line_no);
  return rtn_name + " " + file_name + ":" + buff;
}

class ScopeTimer {
 public:
  ScopeTimer(const char * what) : what_(what) {
    start_ = VG_(read_millisecond_timer)();
  }
  ~ScopeTimer() {
    UInt end = VG_(read_millisecond_timer)();
    Printf("%s: %,dms\n", what_, end - start_);
  }
 private:
  const char * what_;
  UInt start_;
};

//--------- ID ---------------------- {{{1
// We wrap int32_t into ID class and then inherit various ID type from ID.
// This is done in an attempt to implement type safety of IDs, i.e. 
// to make it impossible to make implicit cast from one ID type to another.
class ID {
 public:
  typedef int32_t T;
  explicit ID(T id) : id_(id) {}
  ID (const ID &id) : id_(id.id_) {}
  bool operator ==  (const ID &id) const { return id_ == id.id_; }
  bool operator !=  (const ID &id) const { return id_ != id.id_; }
  bool operator <  (const ID &id) const { return id_ < id.id_; }
  bool operator >  (const ID &id) const { return id_ > id.id_; }
  bool operator >=  (const ID &id) const { return id_ >= id.id_; }
  bool operator <=  (const ID &id) const { return id_ <= id.id_; }

  bool IsValid() const { return id_ >= 0; }

  const ID &operator = (const ID &id) {
    this->id_ = id.id_;
    return *this;
  }
  T raw() const { return id_; }
 private: 
  T id_;
};

// Thread ID.
// id >= 0
class TID: public ID {
 public:
  static const int32_t kInvalidTID;

  explicit TID(T id) : ID(id) {}
  TID() : ID(kInvalidTID) {}
  bool valid() const { return raw() >= 0; }
};

const int32_t TID::kInvalidTID = -1;

// Segment ID.
// id > 0 && id < kMaxSID
class SID: public ID {
 public:
  explicit SID(T id) : ID(id) {}
  SID() : ID(0) {}
  bool valid() const { return raw() > 0 && raw() < kMaxSID; }
};

// Lock ID. 
// id > 0 && id < kMaxLID
class LID: public ID {
 public:
  explicit LID(T id) : ID(id) {}
  LID() : ID(0) {}
  bool valid() const { return raw() > 0 && raw() < kMaxLID; }
};

// LockSet ID.
// Empty lockset: id == 0
// Singleton:     id > 0 (id == Lock's id)
// Tuple:         id < 0
class LSID: public ID {
 public:
  explicit LSID(T id) : ID(id) {}
  LSID() : ID(INT_MAX) {}
  bool valid() const { 
    return raw() < kMaxLID && raw() > -(kMaxLID); 
  }
  bool IsEmpty() const { return raw() == 0; }
  bool IsSingleton() const { return raw() > 0; }
  LID Singleton() const { return LID(raw()); }
};

// SegmentSet ID.
// Empty SegmentSet: id == 0
// Singleton:        id > 0 (id == Segment's id)
// Tuple:            id < 0
class SSID: public ID {
 public:
  explicit SSID(T id) : ID(id) {}
  explicit SSID(SID sid) : ID(sid.raw()) {}
  SSID(): ID(INT_MAX) {}
  bool valid() const { 
    return raw() != 0 && raw() < kMaxSID && raw() > -kMaxSID; 
  }
  bool IsValidOrEmpty() { return raw() < kMaxSID && raw() > -kMaxSID; }
  bool IsEmpty() const { return raw() == 0; }
  bool IsSingleton() const {return raw() > 0; }
  bool IsTuple() const {return raw() < 0; }
  SID  Singleton() const { 
    DCHECK(IsSingleton());
    return SID(raw()); 
  }
  // TODO(timurrrr): need to start SegmentSetArray indices from 1
  // to avoid "int ???() { return -raw() - 1; }"
};

//--------- Colors ----------------------------- {{{1
// Colors for ansi terminals and for html. 
const char *c_bold    = "";
const char *c_red     = "";
const char *c_green   = "";
const char *c_magenta = "";
const char *c_cyan    = "";
const char *c_blue    = "";
const char *c_yellow  = "";
const char *c_default = "";


//--------- IntPairToBoolCache ------ {{{1
template <int kSize>
class IntPairToBoolCache {
 public:
  IntPairToBoolCache() {
    Flush();
  }
  void Flush() {
    memset(arr_, 0, sizeof(arr_));
  }
  void Insert(int a, int b, bool val) {
    uint64_t comb = combine2(a,b);
    uint64_t idx  = comb % kSize;
    if (val) {
      comb |= 1ULL << 63;
    }
    arr_[idx] = comb;
  }
  bool Lookup(int a, int b, bool *val) {
    uint64_t comb = combine2(a,b);
    uint64_t idx  = comb % kSize;
    uint64_t prev = arr_[idx];
    uint64_t valbit = prev & (1ULL << 63);
    if ((prev & ~(1ULL << 63)) == comb) {
      *val = valbit != 0;
      return true;
    }
    return false;
  }
 private:
  uint64_t combine2(int a, int b) {
    CHECK(a > 0);
    CHECK(b > 0);
    int64_t x = a;
    return (x << 32) | b;
  } 
  uint64_t arr_[kSize];
};

//--------- IntPairToIntCache ------ {{{1
template <int kHtableSize, int kArraySize = 8>
class IntPairToIntCache {
 public:
  IntPairToIntCache() {
    Flush();
  }
  void Flush() {
    memset(this, 0, sizeof(*this));
  }
  void Insert(int a, int b, int v) {
    // fill the hash table
    uint32_t idx  = compute_idx(a, b);
    htable_[idx+0] = a;
    htable_[idx+1] = b;
    htable_[idx+2] = v;
    
    // fill the array
    int dummy;
    if (kArraySize != 0 && !ArrayLookup(a, b, &dummy)) {
      int pos = array_pos_;
      array_[3*pos+0] = a;
      array_[3*pos+1] = b;
      array_[3*pos+2] = v;
      array_pos_ = (array_pos_ + 1) % (kArraySize);
    }
  }
  bool Lookup(int a, int b, int *v) {
    // check the array
    if (kArraySize != 0 && ArrayLookup(a, b, v)) {
      G_stats->ls_cache_fast++;
      return true;
    }
    // check the hash table.
    uint32_t idx  = compute_idx(a, b);
    int prev_a = htable_[idx+0];
    int prev_b = htable_[idx+1];
    if (prev_a == a && prev_b == b) {
      *v = htable_[idx+2];
      return true;
    }
    return false;
  }
 private:

  bool ArrayLookup(int a, int b, int *v) {
    for (int i = 0; i < kArraySize; i++) {  
      if (a == array_[3*i+0] && b == array_[3*i+1]) {
        *v = array_[3*i+2]; 
        return true;
      }
    }
    return false;
  }

  uint32_t compute_idx(int a, int b) {
    return 3 * (combine2(a, b) % kHtableSize);
  }
  uint32_t combine2(int a, int b) {
    return (a << 16) ^ b;
  }

  int htable_[kHtableSize * 3];

  int array_[kArraySize * 3];
  int array_pos_;
};

//--------- FreeList --------------- {{{1
class FreeList {
 public:
  FreeList(int obj_size, int chunk_size) 
    : list_(0),
      obj_size_(obj_size), 
      chunk_size_(chunk_size) {
    CHECK(obj_size_ >= (int)sizeof(void*));
    CHECK((obj_size_ % sizeof(void*)) == 0);
    CHECK(chunk_size_ >= 1);
  }

  void *Allocate() {
    if (!list_) 
      AllocateNewChunk();
    CHECK(list_);
    List *head = list_;
    list_ = list_->next;
    return (void*)head;
  }

  void Deallocate(void *ptr) {
    if (DEBUG_MODE) {
      memset(ptr, 0xac, obj_size_);
    }
    List *new_head = (List*)ptr;
    new_head->next = list_;
    list_ = new_head;
  }
 private:
  void AllocateNewChunk() {
    CHECK(list_ == NULL);
    uint8_t *new_mem = new uint8_t[obj_size_ * chunk_size_];
    if (DEBUG_MODE) {
      memset(new_mem, 0xab, obj_size_ * chunk_size_);
    }
    for (int i = 0; i < chunk_size_; i++) {
      List *new_head = (List*)(new_mem + obj_size_ * i);
      new_head->next = list_;
      list_ = new_head;
    }
  }
  struct List {
    struct List *next;
  };
  List *list_;


  const int obj_size_;
  const int chunk_size_;
};
//--------- StackTrace -------------- {{{1
class StackTraceFreeList {
 public:
  uintptr_t *GetNewMemForStackTrace(size_t capacity) {
    DCHECK(capacity <= (size_t)G_flags->num_callers);
    return (uintptr_t*)free_lists_[capacity]->Allocate();
  }
  void TakeStackTraceBack(uintptr_t *mem, size_t capacity) {
    DCHECK(capacity <= (size_t)G_flags->num_callers);  
    free_lists_[capacity]->Deallocate(mem);
  }
  
  StackTraceFreeList() {
    size_t n = G_flags->num_callers + 1;
    free_lists_ = new FreeList *[n];
    free_lists_[0] = NULL;
    for (size_t i = 1; i < n; i++) {
      free_lists_[i] = new FreeList((i+2) * sizeof(uintptr_t), 1024);
    }
  }
 private: 
  
  FreeList **free_lists_; // Array of G_flags->num_callers lists.
};

static StackTraceFreeList *g_stack_trace_free_list;

class StackTrace {
 public:
  static StackTrace *CreateNewEmptyStackTrace(size_t size, 
                                              size_t capacity = 0) {
    ScopedMallocCostCenter cc("StackTrace::CreateNewEmptyStackTrace()");
    DCHECK(g_stack_trace_free_list);
    if (capacity == 0) 
      capacity = size;
    uintptr_t *mem = g_stack_trace_free_list->GetNewMemForStackTrace(capacity);
    DCHECK(mem);
    StackTrace *res = new (mem) StackTrace(size, capacity);
    return res;
  }

  static void Delete(StackTrace *trace) {
    if (!trace) return;
    DCHECK(g_stack_trace_free_list);
    g_stack_trace_free_list->TakeStackTraceBack((uintptr_t*)trace, trace->capacity());
  }

  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }

  void set_size(size_t size) {
    CHECK(size <= capacity());
    size_ = size;
  }


  void Set(size_t i, uintptr_t pc) {
    arr_[i] = pc;
  }

  uintptr_t Get(size_t i) const {
    return arr_[i];
  }


  static string EmbeddedStackTraceToString(const uintptr_t *emb_trace, size_t n,
                                           const char *indent = "    ") {
    string res = "";
    for (size_t i = 0; i < n; i++) {
      const int kBuffSize = 10000;
      char buff[kBuffSize];
      if (!emb_trace[i]) break;
      string rtn_and_file = PcToRtnNameAndFilePos(emb_trace[i]);

      if (i == 0) res += c_bold;
      if (G_flags->show_pc) {
        snprintf(buff, kBuffSize, "%s#%-2d %p: ", 
                 indent, (int)i, (void*)emb_trace[i]);
      } else {
        snprintf(buff, kBuffSize, "%s#%-2d ", indent, (int)i);
      }
      res += buff;

      res += rtn_and_file;
      if (i == 0) res += c_default;
      res += "\n";

      // don't print after main
      if (rtn_and_file.find("main ") == 0)
        break;
      // ... and after the default thread func. TODO: customize it.
      if (rtn_and_file.find("Thread::ThreadBody(") == 0 ||
          rtn_and_file.find("ThreadSanitizerStartThread") == 0 ||
          rtn_and_file.find("start_thread ") == 0) {
        break;
      }
    }
    return res;
  }

  string ToString(const char *indent = "    ") const {
    if (!this) return "NO STACK TRACE\n";
    if (size() == 0) return "EMPTY STACK TRACE\n";
    return EmbeddedStackTraceToString(arr_, size(), indent);
  }
  
  void PrintRaw() const {
    for (size_t i = 0; i < size(); i++) {
      Printf("%p ", arr_[i]); 
    }
    Printf("\n");
  }

  ExeContext *ToValgrindExeContext() {
    return VG_(make_ExeContext_from_StackTrace)((Addr*)arr_, size_);
  }
  
  struct Less {
    bool operator () (const StackTrace *t1, const StackTrace *t2) const {
      return lexicographical_compare_3way (t1->arr_, t1->arr_ + t1->size(),
                                           t2->arr_, t2->arr_ + t2->size()) < 0;
    }
  };

 private:
  StackTrace(size_t size, size_t capacity) 
    : size_(size),
      capacity_(capacity) {
  }

  ~StackTrace() {}

  size_t size_;
  size_t capacity_;
  uintptr_t arr_[]; 
};



//--------- Lock -------------------- {{{1
const char *kLockAllocCC = "kLockAllocCC";
class Lock {
 public:

  static Lock *Create(uintptr_t lock_addr) {
    ScopedMallocCostCenter cc("LockLookup");
//    Printf("Lock::Create: %p\n", lock_addr);
    // Destroy(lock_addr);

    // CHECK(Lookup(lock_addr) == NULL);
    Lock *res = LookupOrCreate(lock_addr);
    res->rd_held_ = 0;
    res->wr_held_ = 0;
    res->is_pure_happens_before_ = false;
    res->last_lock_site_ = NULL;
    return res;
  }

  static void Destroy(uintptr_t lock_addr) {
//    Printf("Lock::Destroy: %p\n", lock_addr);
  //  map_.erase(lock_addr);
  }

  static NOINLINE Lock *LookupOrCreate(uintptr_t lock_addr) {
    ScopedMallocCostCenter cc("LockLookup");
    Lock **lock = &(*map_)[lock_addr];
    if (*lock == NULL) {
//      Printf("Lock::LookupOrCreate: %p\n", lock_addr);
      ScopedMallocCostCenter cc_lock("new Lock");
      *lock = new Lock(lock_addr, map_->size());
    }
    return *lock;
  }

  static NOINLINE Lock *Lookup(uintptr_t lock_addr) {
    ScopedMallocCostCenter cc("LockLookup");
    Map::iterator it = map_->find(lock_addr);
    if (it == map_->end()) return NULL;
    return it->second;
  }

  int       rd_held()   const { return rd_held_; } 
  int       wr_held()   const { return wr_held_; }
  uintptr_t lock_addr() const { return lock_addr_; }
  LID       lid()       const { return lid_; }
  bool is_pure_happens_before() const { return is_pure_happens_before_; }

  void set_is_pure_happens_before(bool x) { is_pure_happens_before_ = x; }

  void WrLock(StackTrace *lock_site) { 
    CHECK(!rd_held_);
    wr_held_++;
    StackTrace::Delete(last_lock_site_);
    last_lock_site_ = lock_site;
  }

  void WrUnlock() {
    CHECK(!rd_held_);
    CHECK(wr_held_);
    wr_held_--;
  }

  void RdLock(StackTrace *lock_site) {
    CHECK(!wr_held_);
    rd_held_++;
    StackTrace::Delete(last_lock_site_);
    last_lock_site_ = lock_site;
  }

  void RdUnlock() {
    CHECK(!wr_held_);
    CHECK(rd_held_);
    rd_held_--;
  }

  string ToString() const {
    char buff[100];
    sprintf(buff, "L%d (%p)", lid_.raw(), (void*)lock_addr());
    return string(buff);
  }

  static void ReportLockWithOrWithoutContext(LID lid, bool with_context) {
    if (!with_context) {
      Report("   L%d\n", lid.raw());
      return;
    }
    // slow, but needed only for reports.
    Lock *lock = NULL;
    for (Map::iterator it = map_->begin(); it != map_->end(); ++it) {
      Lock *l = it->second;
      if (l->lid_ == lid) {
        lock = l;
        break;
      }
    }
    CHECK(lock);
    if(lock->last_lock_site_) {
      Report("   %s\n%s", 
             lock->ToString().c_str(),
             lock->last_lock_site_->ToString().c_str());
    } else {
      Report("   %s. This lock was probably destroyed"
                 " w/o calling Unlock()\n", lock->ToString().c_str());
    }
  }

  static void InitClassMembers() {
    map_ = new Lock::Map;
  }

 private:
  Lock(uintptr_t lock_addr, int32_t lid) 
    : lock_addr_(lock_addr),
      lid_(lid),
      rd_held_(0),
      wr_held_(0),
      is_pure_happens_before_(false),
      last_lock_site_(0) {
  }

  // Data members
  uintptr_t lock_addr_;
  LID       lid_;
  int       rd_held_;
  int       wr_held_;
  bool      is_pure_happens_before_;
  StackTrace *last_lock_site_;

  // Static members
  typedef map<uintptr_t, Lock*> Map;
  static Map *map_;
};


Lock::Map *Lock::map_;

//--------- LockSet ----------------- {{{1
class LockSet {
 public:
  NOINLINE static LSID Add(LSID lsid, Lock *lock) {
    ScopedMallocCostCenter cc("LockSetAdd");
    LID lid = lock->lid();
    if (lsid.IsEmpty()) {
      // adding to an empty lock set
      G_stats->ls_add_to_empty++;
      return LSID(lid.raw());
    }
    int cache_res;
    if (ls_add_cache_->Lookup(lsid.raw(), lid.raw(), &cache_res)) {
      G_stats->ls_add_cache_hit++;
      return LSID(cache_res);
    }
    LSID res;
    if (lsid.IsSingleton()) {
      LSSet set;
      set.insert(lsid.Singleton());
      set.insert(lid);
      G_stats->ls_add_to_singleton++;
      res = ComputeId(set);
    } else {
      LSSet set = Get(lsid);
      set.insert(lid);
      G_stats->ls_add_to_multi++;
      res = ComputeId(set);
    }
    ls_add_cache_->Insert(lsid.raw(), lid.raw(), res.raw());
    return res;
  }
 
  // If lock is present in lsid, set new_lsid to (lsid \ lock) and return true.
  // Otherwise set new_lsid to lsid and return false.
  NOINLINE static bool Remove(LSID lsid, Lock *lock, LSID *new_lsid) {
    *new_lsid = lsid;
    if (lsid.IsEmpty()) return false;
    LID lid = lock->lid();

    if (lsid.IsSingleton()) {
      // removing the only lock -> LSID(0)
      if (lsid.Singleton() != lid) return false;
      G_stats->ls_remove_from_singleton++;
      *new_lsid = LSID(0);
      return true;
    }

    int cache_res;
    if (ls_rem_cache_->Lookup(lsid.raw(), lid.raw(), &cache_res)) {
      G_stats->ls_rem_cache_hit++;
      *new_lsid = LSID(cache_res);
      return true;
    }

    LSSet set = Get(lsid);
    LSSet::iterator it = set.find(lid);
    if (it == set.end()) return false;
    set.erase(it);
    G_stats->ls_remove_from_multi++;
    LSID res = ComputeId(set);
    ls_rem_cache_->Insert(lsid.raw(), lid.raw(), res.raw());
    *new_lsid = res;
    return true;
  }

  NOINLINE static bool IntersectionIsEmpty(LSID lsid1, LSID lsid2) {
    // at least one empty
    if (lsid1.IsEmpty() || lsid2.IsEmpty()) 
      return true;  // empty

    // both singletons
    if (lsid1.IsSingleton() && lsid2.IsSingleton()) {
      return lsid1 != lsid2;
    }

    // first is singleton, second is not
    if (lsid1.IsSingleton()) {
      const LSSet &set2 = Get(lsid2);
      return set2.count(LID(lsid1.raw())) == 0;
    }

    // second is singleton, first is not
    if (lsid2.IsSingleton()) {
      const LSSet &set1 = Get(lsid1);
      return set1.count(LID(lsid2.raw())) == 0;
    }

    // both are not singletons
    const LSSet &set1 = Get(lsid1);
    const LSSet &set2 = Get(lsid2);

    LID intersection[min(set1.size(), set2.size())];
    LID *end = set_intersection(set1.begin(), set1.end(), 
                            set2.begin(), set2.end(), 
                            intersection);
    return end == intersection;
  }

  NOINLINE static LSID Intersect(LSID lsid1, LSID lsid2) {
    // TODO: avoid code duplication with IntersectionIsEmpty().
    ScopedMallocCostCenter cc("LockSet::Intersect");
    // at least one empty
    if (lsid1.IsEmpty() || lsid2.IsEmpty()) 
      return LSID(0);  // empty

    // both singletons
    if (lsid1.IsSingleton() && lsid2.IsSingleton()) {
      return lsid1 == lsid2 ? lsid1 : LSID(0);
    }

    // first is singleton, second is not
    if (lsid1.IsSingleton()) {
      const LSSet &set2 = Get(lsid2);
      return set2.count(LID(lsid1.raw())) ? lsid1 : LSID(0);
    }

    // second is singleton, first is not
    if (lsid2.IsSingleton()) {
      const LSSet &set1 = Get(lsid1);
      return set1.count(LID(lsid2.raw())) ? lsid2 : LSID(0);
    }

    // both are not singletons
    const LSSet &set1 = Get(lsid1);
    const LSSet &set2 = Get(lsid2);

    LID intersection[min(set1.size(), set2.size())];
    LID *end = set_intersection(set1.begin(), set1.end(), 
                                set2.begin(), set2.end(), 
                                intersection);
    if (intersection == end) {
      return LSID(0); // empty
    }
    if (end - intersection == 1) {
      return LSID(intersection[0].raw());  // Singleton
    }
    LSSet set(intersection, end);
    return ComputeId(set);
  }




  static string ToString(LSID lsid) {
    char buff[1000];
    if (lsid.IsEmpty()) {
      return "{}";
    } else if (lsid.IsSingleton()) {
      sprintf(buff, "{L%d}", lsid.raw());
      return buff;
    }
    const LSSet &set = Get(lsid);
    sprintf(buff, "{");
    string res = buff;
    bool first = true;
    for (LSSet::const_iterator it = set.begin(); it != set.end(); ++it) {
      if (!first) res += ", ";
      sprintf(buff, "L%d", it->raw());
      res += buff;
      first = false;
    }
    res += "}";
    return res;
  }

  static void ReportLockSetWithContexts(LSID lsid, 
                                        set<LID> *locks_reported,
                                        const char *descr) {
    if (lsid.IsEmpty()) return;
    Report("%s%s%s\n", c_green, descr, c_default);
    if (lsid.IsSingleton()) { 
      LID lid = lsid.Singleton();
      Lock::ReportLockWithOrWithoutContext(lid,
                                           locks_reported->count(lid) == 0);
      locks_reported->insert(lid);
    } else {
      const LSSet &set = Get(lsid);
      for (LSSet::const_iterator it = set.begin(); it != set.end(); ++it) {
        LID lid = *it;
        Lock::ReportLockWithOrWithoutContext(lid, 
                                             locks_reported->count(lid) == 0);
        locks_reported->insert(lid);
      }
    }
  }

  static void AddLocksToSet(LSID lsid, set<LID> *locks) {
    if (lsid.IsEmpty()) return;
    if (lsid.IsSingleton()) { 
      locks->insert(lsid.Singleton());
    } else {
      const LSSet &set = Get(lsid);
      for (LSSet::const_iterator it = set.begin(); it != set.end(); ++it) {
        locks->insert(*it);
      }
    }
  }


  static void InitClassMembers() {
    map_ = new LockSet::Map;
    vec_ = new LockSet::Vec;
    ls_add_cache_ = new LSCache;
    ls_rem_cache_ = new LSCache;
    ls_rem_cache_ = new LSCache;
  }

 private:
  // No instances are allowed.
  LockSet() { }

  typedef multiset<LID> LSSet;

  static LSSet &Get(LSID lsid) {
    int idx = -lsid.raw() - 1;
    DCHECK(idx >= 0);
    DCHECK(idx < (int)vec_->size());
    return (*vec_)[idx];
  }

  static LSID ComputeId(const LSSet &set) {
    if (set.empty()) {
      // empty lock set has id 0.
      return LSID(0); 
    }
    if (set.size() == 1) {
      // signleton lock set has lsid == lid.
      return LSID(set.begin()->raw());
    }
    DCHECK(map_);
    DCHECK(vec_);
    // multiple locks.
    int32_t *id = &(*map_)[set];
    if (*id == 0) {
      vec_->push_back(set);
      *id = map_->size();
    }
    return LSID(-*id);
  }


  typedef map<LSSet, int32_t> Map;
  static Map *map_;
    
  static const char *kLockSetVecAllocCC;
  typedef vector<LSSet, CCAlloc<LSSet, &kLockSetVecAllocCC> > Vec;
  static Vec *vec_;

//  static const int kPrimeSizeOfLsCache = 307;
//  static const int kPrimeSizeOfLsCache = 499;
  static const int kPrimeSizeOfLsCache = 1021;
  typedef IntPairToIntCache<kPrimeSizeOfLsCache> LSCache;
  static LSCache *ls_add_cache_;
  static LSCache *ls_rem_cache_;
  static LSCache *ls_int_cache_;

};

LockSet::Map *LockSet::map_;
LockSet::Vec *LockSet::vec_;
const char *LockSet::kLockSetVecAllocCC = "kLockSetVecAllocCC";
LockSet::LSCache *LockSet::ls_add_cache_;
LockSet::LSCache *LockSet::ls_rem_cache_;
LockSet::LSCache *LockSet::ls_int_cache_;


static string TwoLockSetsToString(LSID rd_lockset, LSID wr_lockset) {
  string res;
  if (rd_lockset == wr_lockset) {
    res = "locks held: ";
    res += LockSet::ToString(wr_lockset);
  } else {
    res = "writer locks held: ";
    res += LockSet::ToString(wr_lockset);
    res += "; reader locks held: ";
    res += LockSet::ToString(rd_lockset);
  }
  return res;
}




//--------- VTS ------------------ {{{1

class VTS {
 public:
  static size_t MemoryRequiredForOneVts(size_t size) {
    return sizeof(VTS) + size * sizeof(TS);
  }
  
  static size_t RoundUpSizeForEfficientUseOfFreeList(size_t size) {
    if (size < 32) return size;
    if (size < 64) return (size + 7) & ~7;
    if (size < 128) return (size + 15) & ~15;
    return (size + 31) & ~31;
  }

  static VTS *Create(size_t size) {
    DCHECK(size > 0);
    void *mem;
    size_t rounded_size = RoundUpSizeForEfficientUseOfFreeList(size);
    DCHECK(size <= rounded_size);
    if (rounded_size <= kNumberOfFreeLists) {
      // Small chunk, use FreeList.
      ScopedMallocCostCenter cc("VTS::Create (from free list)");
      mem = free_lists_[rounded_size]->Allocate();
      G_stats->vts_create_small++;
    } else {
      // Large chunk, use new/delete instead of FreeList.
      ScopedMallocCostCenter cc("VTS::Create (from new[])");
      mem = new int8_t[MemoryRequiredForOneVts(size)];
      G_stats->vts_create_big++;
    }
    VTS *res = new (mem) VTS(size);
    G_stats->vts_total_size += size;
    return res;
  }

  // TODO(timurrrr): rename Delete/Clone with Unref/Ref
  static void Delete(VTS *vts) {
    if (!vts) return;
    CHECK(vts->ref_count_ > 0);
    vts->ref_count_--;
    if (vts->ref_count_ == 0) {
      G_stats->vts_delete++;
      size_t size = vts->size_; // can't use vts->size().
      size_t rounded_size = RoundUpSizeForEfficientUseOfFreeList(size);
      if (rounded_size <= kNumberOfFreeLists) {
        free_lists_[rounded_size]->Deallocate(vts);
      } else {
        delete vts; 
      }
    }
  }

  static VTS *CreateSingleton(TID tid, int32_t clk = 1) {
    VTS *res = Create(1);
    res->arr_[0].tid = tid;
    res->arr_[0].clk = clk;
    return res;
  }
  
  VTS *Clone() {
    G_stats->vts_clone++;
    ref_count_++;
    return this;
  }

  static VTS *CopyAndTick(const VTS *vts, TID id_to_tick) {
    CHECK(vts->ref_count_);
    VTS *res = Create(vts->size());
    bool found = false;
    for (size_t i = 0; i < res->size(); i++) {
      res->arr_[i] = vts->arr_[i];
      if (res->arr_[i].tid == id_to_tick) {
        res->arr_[i].clk++;
        found = true;
      }
    }
    CHECK(found);
    return res;
  }

  static VTS *JoinAndTick(const VTS *vts_a, const VTS *vts_b, TID id_to_tick) {
    CHECK(vts_a->ref_count_);
    CHECK(vts_b->ref_count_);
    //Printf("JoinAndTick: id_to_tick=%d\n%s\n%s\n", 
    //       id_to_tick.raw(), 
    //       vts_a->ToString().c_str(), 
    //       vts_b->ToString().c_str());
    bool found = false;
    TS result_ts[vts_a->size() + vts_b->size()];
    TS *t = result_ts;
    const TS *a = &vts_a->arr_[0];
    const TS *b = &vts_b->arr_[0];
    const TS *a_max = a + vts_a->size();
    const TS *b_max = b + vts_b->size();
    // TODO(timurrrr): comment the loop below.
    while (a < a_max && b < b_max) {
      if (a->tid < b->tid) {
        *t = *a;
        if (t->tid == id_to_tick) { 
          t->clk++;
          found = true;
        }
        a++;
        t++;
      } else if (a->tid > b->tid) {
        *t = *b;
        CHECK(t->tid != id_to_tick);
        b++;
        t++;
      } else {
        if (a->clk >= b->clk) {
          *t = *a;
          if (t->tid == id_to_tick) {
            t->clk++;
            found = true;
          }
        } else {
          *t = *b;
          CHECK(t->tid != id_to_tick);
        }
        a++;
        b++;
        t++;
      }
    }
    while (a < a_max) {
      *t = *a;
      if (t->tid == id_to_tick) {
        t->clk++;
        found = true;
      }
      a++;
      t++;
    }
    while (b < b_max) {
      *t = *b;
      CHECK(t->tid != id_to_tick);
      b++;
      t++;
    }

    if (id_to_tick.IsValid()) { CHECK(found == true); }

    VTS *res = VTS::Create(t - result_ts);
    for (size_t i = 0; i < res->size(); i++) {
      res->arr_[i] = result_ts[i];
    }
    return res;
  }

  static INLINE bool HappensBeforeCached(const VTS *vts_a, const VTS *vts_b) {
    bool res = false;
    if (hb_cache_->Lookup(vts_a->uniq_id_, vts_b->uniq_id_, &res)) {
      G_stats->n_vts_hb_cached++;
      return res;
    }
    res = HappensBefore(vts_a, vts_b);
    hb_cache_->Insert(vts_a->uniq_id_, vts_b->uniq_id_, res);
    return res;
  }

  // return true if vts_a happens-before vts_b.
  static NOINLINE bool HappensBefore(const VTS *vts_a, const VTS *vts_b) {
    CHECK(vts_a->ref_count_);
    CHECK(vts_b->ref_count_);
    G_stats->n_vts_hb++;
    const TS *a = &vts_a->arr_[0];
    const TS *b = &vts_b->arr_[0];
    const TS *a_max = a + vts_a->size();
    const TS *b_max = b + vts_b->size();
    bool a_less_than_b = false;
    while (a < a_max && b < b_max) {
      if (a->tid < b->tid) {
        // a->tid is not present in b. 
        return false;
      } else if (a->tid > b->tid) {
        // b->tid is not present in a.
        a_less_than_b = true;
        b++;
      } else {
        // this tid is present in both VTSs. Compare clocks.
        if (a->clk > b->clk) return false; 
        if (a->clk < b->clk) a_less_than_b = true;
        a++;
        b++;
      }
    }
    if (a < a_max) {
      // Some tids are present in a and not in b
      return false;
    }
    if (b < b_max) {
      return true;
    }
    return a_less_than_b;
  }

  size_t size() const { 
    DCHECK(ref_count_);
    return size_;
  }

  string ToString() const {
    DCHECK(ref_count_);
    string res = "[";
    for (size_t i = 0; i < size(); i++) {
      char buff[100];
      sprintf(buff, "%d:%d;", arr_[i].tid.raw(), arr_[i].clk);
      if (i) res += " ";
      res += buff;
    }
    return res + "]";
  }
  
  void print(const char *name) const {
    string str = ToString();
    Printf("%s: %s\n", name, str.c_str());
  }

  static void TestHappensBefore() {
    // TODO: need more tests here... 
    static const char *test_vts[] = {
      "[0:1;]",
      "[0:4; 2:1;]",
      "[0:4; 2:2; 4:1;]",
      "[0:4; 3:2; 4:1;]",
      "[0:4; 3:2; 4:2;]",
      "[0:4; 3:3; 4:1;]",
      NULL
    };  

    for (int i = 0; test_vts[i]; i++) {
      const VTS *vts1 = Parse(test_vts[i]);
      for (int j = 0; test_vts[j]; j++) {
        const VTS *vts2 = Parse(test_vts[j]);
        bool hb  = HappensBefore(vts1, vts2);
        Printf("HB = %d\n   %s\n   %s\n", (int)hb, 
               vts1->ToString().c_str(),
               vts2->ToString().c_str());
        delete vts2;
      }
      delete vts1;
    }
  
  }

  static void Test() {
    Printf("VTS::test();\n");
    VTS *v1 = CreateSingleton(TID(0));
    VTS *v2 = CreateSingleton(TID(1));
    VTS *v3 = CreateSingleton(TID(2));
    VTS *v4 = CreateSingleton(TID(3));

    VTS *v12 = JoinAndTick(v1, v2, TID());
    v12->print("v12"); 
    VTS *v34 = JoinAndTick(v3, v4, TID());
    v34->print("v34");

    VTS *all = JoinAndTick(v12, v34, TID(1));
    all->print("all");
    VTS *all2 = CopyAndTick(all, TID(3));
    all2->print("al2");

    VTS *x1 = Parse("[0:4; 3:6; 4:2;]");
    CHECK(x1);
    x1->print("x1");
    TestHappensBefore();
  }

  // Parse VTS string in the form "[0:4; 3:6; 4:2;]".
  static VTS *Parse(const char *str) {
#if 1 // TODO(kcc): need sscanf in valgrind
    return NULL;
#else
    vector<TS> vec;
    if (!str) return NULL;
    if (str[0] != '[') return NULL;
    str++;
    int tid = 0, clk = 0;
    int consumed = 0;
    while (sscanf(str, "%d:%d;%n", &tid, &clk, &consumed) > 0) {
      TS ts;
      ts.tid = TID(tid);
      ts.clk = clk;
      vec.push_back(ts);
      str += consumed;
      // Printf("%d:%d\n", tid, clk);
    }
    if (*str != ']') return NULL;
    VTS *res = Create(vec.size());
    for (size_t i = 0; i < vec.size(); i++) {
      res->arr_[i] = vec[i];
    }
    return res;
#endif
  }

  static void InitClassMembers() {
    hb_cache_ = new HBCache;
    free_lists_ = new FreeList *[kNumberOfFreeLists+1];
    free_lists_[0] = 0;
    for (size_t  i = 1; i <= kNumberOfFreeLists; i++) {
      free_lists_[i] = new FreeList(MemoryRequiredForOneVts(i), 
                                    (kNumberOfFreeLists * 4) / i);
    }
  }

 private:
  VTS(size_t size) 
    : ref_count_(1), 
      size_(size) {
    uniq_id_counter_++;
    // If we've got overflow, we are in trouble, need to have 64-bits...
    CHECK(uniq_id_counter_ > 0);  
    uniq_id_ = uniq_id_counter_;
  }
  ~VTS() {}

  struct TS {
    TID     tid;
    int32_t clk;
  };


  // data members
  int32_t ref_count_;
  int32_t uniq_id_;
  size_t size_;
  TS     arr_[];  // array of size_ elements.


  // static data members
  static int32_t uniq_id_counter_;
  static const int kCacheSize = 4999;  // Has to be prime.
  typedef IntPairToBoolCache<kCacheSize> HBCache;
  static HBCache *hb_cache_;

  static const size_t kNumberOfFreeLists = 512; // Must be power of two.
//  static const size_t kNumberOfFreeLists = 64; // Must be power of two.
  static FreeList **free_lists_;  // Array of kNumberOfFreeLists elements.
};

int32_t VTS::uniq_id_counter_;
VTS::HBCache *VTS::hb_cache_;
FreeList **VTS::free_lists_;



//--------- Mask -------------------- {{{1
class Mask {
 public: 
  Mask() : m_(0) {}
  Mask(const Mask &m) : m_(m.m_) { }
  explicit Mask(uintptr_t m) : m_(m) { }
  bool Get(uintptr_t idx) const   { return m_ & (1ULL << idx); }
  void Set(uintptr_t idx)   { m_ |= 1ULL << idx; }
  void Clear(uintptr_t idx) { m_ &= ~(1ULL << idx); } 
  bool Empty() const {return m_ == 0; }

  // Clear bits in range [a,b) and return old [a,b) range.
  Mask ClearRangeAndReturnOld(uintptr_t a, uintptr_t b) {
    DCHECK(a < b);
    DCHECK(b <= 64);
    uintptr_t res;
    uintptr_t n_bits_in_mask = (b - a);
    if (n_bits_in_mask == 64) {
      res = m_;
      m_ = 0;
    } else {
      uintptr_t t = (1ULL << n_bits_in_mask);
      uintptr_t mask = (t - 1) << a;
      res = m_ & mask;
      m_ &= ~mask;
    }
    return Mask(res);
  }

  void ClearRange(uintptr_t a, uintptr_t b) {
    ClearRangeAndReturnOld(a, b);
  }

  void SetRange(uintptr_t a, uintptr_t b) {
    DCHECK(a < b);
    DCHECK(b <= 64);
    uintptr_t n_bits_in_mask = (b - a);
    if (n_bits_in_mask == 64) {
      m_ = ~0;
    } else {
      uintptr_t t = (1ULL << n_bits_in_mask);
      uintptr_t mask = (t - 1) << a;
      m_ |= mask;
    }
  }

  uintptr_t GetRange(uintptr_t a, uintptr_t b) const {
    DCHECK(a < b);
    DCHECK(b <= 64);
    uintptr_t n_bits_in_mask = (b - a);
    if (n_bits_in_mask == 64) {
      return m_;
    } else {
      uintptr_t t = (1ULL << n_bits_in_mask);
      uintptr_t mask = (t - 1) << a;
      return m_ & mask;
    }
  }

  void Subtract(Mask m) { m_ &= ~m.m_; } 
  void Union(Mask m) { m_ |= m.m_; }

  static Mask Intersection(Mask m1, Mask m2) { return Mask(m1.m_ & m2.m_); } 
  

  void Clear() { m_ = 0; }


  string ToString() const {
    char buff[65];
    for (int i = 0; i < 64; i++) {
      buff[i] = Get(i) ? '1' : '0';
    }
    buff[64] = 0;
    return buff;
  }

  static void Test() {
    Mask m;
    m.Set(2);
    Printf("%s\n", m.ToString().c_str());
    m.ClearRange(0, 64);
    Printf("%s\n", m.ToString().c_str());
  }

 private:
  uint64_t m_;
};

//--------- Segment -------------------{{{1
class Segment {
 public:

  // non-static methods

  VTS *vts() const { return vts_; }
  TID tid() const { return TID(tid_); }
  uintptr_t *embedded_stack_trace() {return embedded_stack_trace_; }
  LSID  lsid(bool is_w) const { return lsid_[is_w]; }

  // static methods

  static SID AddNewSegment(TID tid, VTS *vts, 
                           LSID rd_lockset, LSID wr_lockset) {
    ScopedMallocCostCenter malloc_cc("Segment::AddNewSegment()");
    DCHECK(vts);
    DCHECK(tid.valid());
    SID sid;
    Segment *seg;
    if (reusable_sids_->empty()) {

      G_stats->seg_create++;
      CHECK(n_segments_ < kMaxSID);
      seg = GetSegmentByIndex(n_segments_);

      // This VTS may not be empty due to ForgetAllState().
      VTS::Delete(seg->vts_);

      if (DEBUG_MODE && G_flags->debug_level >= 3 &&
          (n_segments_ % (1024 * 128)) == 0) {
       Printf("Segment: allocated SID %d\n", n_segments_);
      }
      sid = SID(n_segments_);
      n_segments_++;
    } else {
      G_stats->seg_reuse++;
      sid = reusable_sids_->back();
      reusable_sids_->pop_back();
      seg = GetInternal(sid);
      DCHECK(!seg->ref_count_);
      DCHECK(!seg->vts());
      DCHECK(!seg->tid().valid());
      CHECK(sid.valid());
    }
    DCHECK(seg);
    seg->ref_count_ = 0;
    seg->tid_ = tid;
    seg->lsid_[0] = rd_lockset;
    seg->lsid_[1] = wr_lockset;
    seg->vts_ = vts;
    seg->embedded_stack_trace_[0] = 0;
    DCHECK(seg->vts_);
    return sid;
  }

  static bool Alive(SID sid) {
    Segment *seg = GetInternal(sid);
    return seg->vts() != NULL;
  }

  static void AssertLive(SID sid, int line) {
    if (DEBUG_MODE) {
      if (!(sid.raw() < n_segments_)) {
        Printf("Segment::AssertLive: failed on sid=%d n_segments = %dline=%d\n",
               sid.raw(), n_segments_, line);
      }
      Segment *seg = GetInternal(sid);
      if (!seg->vts()) {
        Printf("Segment::AssertLive: failed on sid=%d line=%d\n", 
               sid.raw(), line);
      }
      DCHECK(seg->vts());
      DCHECK(seg->tid().valid());
    }
  }

  static Segment *Get(SID sid) {
    AssertLive(sid, __LINE__);
    Segment *res = GetInternal(sid);
    DCHECK(res->vts());
    DCHECK(res->tid().valid());
    return res;
  }

  static int NumLiveSegments() {
    return n_segments_ - recycled_sids_->size() - reusable_sids_->size();
  }

  static bool RecycleOneSid(SID sid) {
    ScopedMallocCostCenter malloc_cc("Segment::RecycleOneSid()");
    static const size_t kRecyclePeriod = 10000; // TODO: test it!
    Segment *seg = GetInternal(sid);
    DCHECK(!seg->ref_count_);
    DCHECK(sid.raw() < n_segments_);
    if (!seg->vts()) return false;  // Already recycled.
    seg->tid_ = TID();
    VTS::Delete(seg->vts_);
    seg->vts_ = NULL;
    recycled_sids_->push_back(sid);
    if (recycled_sids_->size() > kRecyclePeriod
        && reusable_sids_->empty()) {
      recycled_sids_->swap(*reusable_sids_);
    }
    return true;
  }

  int ref_count() const {return ref_count_; }

  static void INLINE Ref(SID sid, const char *where) { 
    Segment *seg = GetInternal(sid);
    if (DEBUG_MODE && G_flags->debug_level >= 3)
      Printf("SegRef   : %d ref=%d %s\n", sid.raw(), seg->ref_count_, where);  
    DCHECK(seg->ref_count_ >= 0);
    seg->ref_count_++; 
  }
  static void INLINE Unref(SID sid, const char *where) { 
    Segment *seg = GetInternal(sid);
    if (DEBUG_MODE && G_flags->debug_level >= 3)
      Printf("SegUnref : %d ref=%d %s\n", sid.raw(), seg->ref_count_, where);  
    DCHECK(seg->ref_count_ > 0);
    seg->ref_count_--; 
    if (seg->ref_count_ == 0) {
      RecycleOneSid(sid);
    }
  } 

  static void ForgetAllState() {
    n_segments_ = 1;
    reusable_sids_->clear();
    recycled_sids_->clear();
    // vts_'es will be freed in AddNewSegment.
  }

  static string ToString(SID sid) {
    char buff[100];
    sprintf(buff, "T%d/S%d", Get(sid)->tid().raw(), sid.raw());
    return buff;
  }

  static string ToStringTidOnly(SID sid) {
    char buff[100];
    sprintf(buff, "T%d", Get(sid)->tid().raw());
    return buff;
  }

  static string ToStringWithLocks(SID sid) {
    char buff[100];
    Segment *seg = Get(sid);
    sprintf(buff, "T%d/S%d ", seg->tid().raw(), sid.raw());
    string res = buff;
    res += TwoLockSetsToString(seg->lsid(false), seg->lsid(true));
    return res;
  }

  static bool INLINE HappensBeforeOrSameThread(SID a, SID b) {
    if (a == b) return true;
    if (Get(a)->tid() == Get(b)->tid()) return true;
    return HappensBefore(a, b);
  }

  static bool INLINE HappensBefore(SID a, SID b) {
    DCHECK(a != b);
    G_stats->n_seg_hb++;
    bool res = false;
    const Segment *seg_a = Get(a);
    const Segment *seg_b = Get(b);
    DCHECK(seg_a->tid() != seg_b->tid());
    const VTS *vts_a = seg_a->vts();
    const VTS *vts_b = seg_b->vts();
    res = VTS::HappensBeforeCached(vts_a, vts_b);
    if (0 && DEBUG_MODE) {
      Printf("HB = %d\n  %s\n  %s\n", res, 
           vts_a->ToString().c_str(), vts_b->ToString().c_str()); 
    }
    return res;
  }

  static int32_t NumberOfSegments() { return n_segments_; }


  static void InitClassMembers() {

    CHECK(sizeof(Segment) == 
          4 * sizeof(int32_t) // ref_count_, tid_, lsid_[2]
          + 1 * sizeof(void*) // vts_
          + 1 * sizeof(uintptr_t) // embedded_stack_trace_[1]
          );
    actual_size_of_segment_ = sizeof(Segment) + 
        (kSizeOfHistoryStackTrace - 1) * sizeof(uintptr_t);
    Report("INFO: Allocating %'ld (%'ld * %'ld) bytes for Segments.\n", 
           actual_size_of_segment_ * kMaxSID, actual_size_of_segment_, kMaxSID);

    all_segments_  = new uint8_t[kMaxSID * actual_size_of_segment_];
    n_segments_    = 1;
    reusable_sids_ = new vector<SID>;
    recycled_sids_ = new vector<SID>;

    // initialize all_segments_[0] with garbage
    memset(all_segments_, -1, actual_size_of_segment_);
  }
 private:
  static Segment *GetSegmentByIndex(int32_t index) {
    return (Segment*)&all_segments_[index * actual_size_of_segment_];
  }
  static Segment *GetInternal(SID sid) {
    DCHECK(sid.valid());
    DCHECK(sid.raw() < n_segments_);
    Segment *res = GetSegmentByIndex(sid.raw());
    return res;
  }

  // Data members.
  uint32_t ref_count_;
  TID      tid_;
  LSID     lsid_[2];
  VTS *vts_;
  uintptr_t embedded_stack_trace_[1];

  // static class members.

  // Array of kMaxSID Segments. The size of the Segment is not known at compile
  // time, but is constant after reading command line flags. So we use an array
  // of kMaxSID*actual_sizeof(Segment) to store segments.
  static uint8_t *all_segments_;

  static size_t actual_size_of_segment_;
  static int32_t n_segments_;
  static vector<SID> *reusable_sids_;
  static vector<SID> *recycled_sids_;
};

uint8_t          *Segment::all_segments_;
size_t            Segment::actual_size_of_segment_;
int32_t           Segment::n_segments_;
vector<SID>      *Segment::reusable_sids_;
vector<SID>      *Segment::recycled_sids_;

//--------- SegmentSet -------------- {{{1
class SegmentSet {
 public:

  static NOINLINE SSID AddSegmentToSS(SSID old_ssid, SID new_sid);
  static INLINE   SSID AddSegmentToSingletonSS(SSID ssid, SID new_sid);
  static INLINE   SSID AddSegmentToTupleSS(SSID ssid, SID new_sid);
  static NOINLINE SSID RemoveSegmentFromSS(SSID old_ssid, SID sid_to_remove);
  static INLINE   SSID RemoveSegmentFromTupleSS(SSID old_ssid, SID sid_to_remove);


  SSID ComputeSSID() {
    SSID res = map_->GetIdOrZero(this);
    CHECK(res.raw() != 0);
    return res;
  }
  

  static void AssertLive(SSID ssid, int line) {
    DCHECK(ssid.valid());
    if (DEBUG_MODE) {
      if (ssid.IsSingleton()) {
        Segment::AssertLive(ssid.Singleton(), line);
      } else {
        DCHECK(ssid.IsTuple());
        int idx = -ssid.raw()-1;
        DCHECK(idx < (int)vec_->size());
        DCHECK(idx >= 0);
        SegmentSet *res = (*vec_)[idx];
        DCHECK(res);
        DCHECK(res->ref_count_ >= 0);

        if (!res) {
          Printf("SegmentSet::AssertLive failed at line %d (ssid=%d)\n", 
                 line, ssid.raw());
          DCHECK(0);
        }
      }
    }
  }

  static SegmentSet *Get(SSID ssid) {
    DCHECK(ssid.valid());
    DCHECK(!ssid.IsSingleton());
    int idx = -ssid.raw()-1;
    DCHECK(idx < (int)vec_->size() && idx >= 0);
    SegmentSet *res = (*vec_)[idx];
    DCHECK(res);
    DCHECK(res->size() >= 2);
    return res;
  }

  void RecycleOneSegmentSet(SSID ssid) {
    DCHECK(ref_count_ == 0);
    DCHECK(ssid.valid());
    DCHECK(!ssid.IsSingleton());
    int idx = -ssid.raw()-1;
    DCHECK(idx < (int)vec_->size() && idx >= 0);
    CHECK((*vec_)[idx] == this);
    // Printf("SegmentSet::RecycleOneSegmentSet: %d\n", ssid.raw());
    //
    // Recycle segments
    int size = this->size();
    for (int i = 0; i < size; i++) {
      SID sid = this->GetSID(i);
      Segment::Unref(sid, "SegmentSet::Recycle");
    }
  
    map_->Erase(this);
    G_stats->ss_recycle++;
  }

  static void INLINE Ref(SSID ssid, const char *where) {
    DCHECK(ssid.valid());
    if (ssid.IsSingleton()) {
      Segment::Ref(ssid.Singleton(), where);
    } else {
      SegmentSet *sset = Get(ssid);
      // Printf("SSRef   : %d ref=%d %s\n", ssid.raw(), sset->ref_count_, where);  
      DCHECK(sset->ref_count_ >= 0);
      sset->ref_count_++;
    }
  }

  static void INLINE RefIfNotEmpty(SSID ssid, const char *where) {
    if (!ssid.IsEmpty()) 
      Ref(ssid, where);
  }

  static void INLINE Unref(SSID ssid, const char *where) {
    DCHECK(ssid.valid());
    if (ssid.IsSingleton()) {
      Segment::Unref(ssid.Singleton(), where);
    } else {
      SegmentSet *sset = Get(ssid);
      // Printf("SSUnref : %d ref=%d %s\n", ssid.raw(), sset->ref_count_, where);
      DCHECK(sset->ref_count_ > 0);
      sset->ref_count_--;
      if (sset->ref_count_ == 0) {
        reused_->push_back(ssid);
      }
    }
  }

  string ToString() const;
  void Print() { 
    Printf("SS%d:%s\n", -ComputeSSID().raw(), ToString().c_str()); 
  }

  static string ToString(SSID ssid) {
    CHECK(ssid.IsValidOrEmpty());
    if (ssid.IsSingleton()) {
      return "{" +  Segment::ToStringTidOnly(SID(ssid.raw())) + "}";
    } else if (ssid.IsEmpty()) {
      return "{}";
    } else {
      AssertLive(ssid, __LINE__);
      return Get(ssid)->ToString();
    }
  }


  static string ToStringWithLocks(SSID ssid);


  static void ForgetAllState() {
    for (size_t i = 0; i < vec_->size(); i++) {
      delete (*vec_)[i];
    }
    map_->Clear();
    vec_->clear();
    reused_->clear();
  }


  static void Test();

  int32_t size() const { return size_; }

  static int32_t Size(SSID ssid) {
    if (ssid.IsEmpty()) return 0;
    if (ssid.IsSingleton()) return 1;
    return Get(ssid)->size(); 
  }

  SID GetSID (int32_t i) const {
    DCHECK(i >= 0 && i < size_);
    return sids_[i];
  }

  void SetSID(int32_t i, SID sid) {
    DCHECK(i >= 0 && i < size_);
    sids_[i] = sid;
  }

  static SID GetSID(SSID ssid, int32_t i, int line) {
    DCHECK(ssid.valid());
    if (ssid.IsSingleton()) {
      DCHECK(i == 0);
      Segment::AssertLive(ssid.Singleton(), line);
      return ssid.Singleton();
    } else {
      AssertLive(ssid, __LINE__);
      SID sid = Get(ssid)->GetSID(i);
      Segment::AssertLive(sid, line);
      return sid;
    }
  }

  static Segment *GetSegmentForNonSingleton(SSID ssid, int32_t i, int line) {
    return Segment::Get(GetSID(ssid, i, line));
  }

  void NOINLINE Validate(int line) const;

  static size_t NumberOfSegmentSets() { return vec_->size(); }


  static void InitClassMembers() {
    map_    = new Map;
    vec_    = new vector<SegmentSet *>; 
    reused_ = new vector<SSID>; 
  }

 private: 
  SegmentSet(int32_t size)  // Private CTOR
    : size_(size),
      ref_count_(0) {
  }  
  SegmentSet() { }

  static INLINE SSID AllocateAndCopy (SegmentSet *ss) {
    int32_t size = ss->size();
    DCHECK(ss->ref_count_ == 0);
    DCHECK(size >= 2);
    DCHECK(size <= kMaxSegmentSetSize);
    DCHECK(sizeof(int32_t) == sizeof(SID));
    SSID res_ssid;
    SegmentSet *res_ss = 0;

    while (reused_->size() > G_flags->segment_set_recycle_queue_size) {
      res_ssid = reused_->back();
      reused_->pop_back();
      int idx = -res_ssid.raw()-1;
      res_ss = (*vec_)[idx];
      DCHECK(res_ss);
      DCHECK(res_ss == Get(res_ssid));
      DCHECK(res_ss->ref_count_ >= 0);
      if (res_ss->ref_count_ == 0) {
        // reuse one
        res_ss->RecycleOneSegmentSet(res_ssid);
        G_stats->ss_reuse++;
        break;
      } else {
        // this SSID is still in use, try another one or create new
        res_ss = 0;
      }
    }

    if (res_ss == 0) {
      // create a new one
      ScopedMallocCostCenter cc("SegmentSet::CreateNewSegmentSet");
      G_stats->ss_create++;
      res_ss = new SegmentSet;
      vec_->push_back(res_ss);
      res_ssid = SSID(-((int32_t)vec_->size()));
      CHECK(res_ssid.valid());
    }
    DCHECK(res_ss);
    res_ss->size_ = size;
    res_ss->ref_count_ = 0;
    DCHECK(res_ss == Get(res_ssid));
    for (int i = 0; i < size; i++) {
      SID sid = ss->GetSID(i);
      Segment::Ref(sid, "SegmentSet::FindExistingOrAlocateAndCopy");
      res_ss->SetSID(i, sid);
    }
    map_->Insert(res_ss, res_ssid);
    return res_ssid;
  }

  static NOINLINE SSID FindExistingOrAlocateAndCopy (SegmentSet *ss) {
    if (DEBUG_MODE) {
      int size = ss->size();
      if (size == 2) G_stats->ss_size_2++;
      if (size == 3) G_stats->ss_size_3++;
      if (size == 4) G_stats->ss_size_4++;
      if (size > 4) G_stats->ss_size_other++;
    }

    // First, check if there is such set already.
    SSID ssid = map_->GetIdOrZero(ss);
    if (ssid.raw() != 0) {  // Found.
      AssertLive(ssid, __LINE__);
      G_stats->ss_find++;
      return ssid;
    }
    // If no such set, create one. 
    return AllocateAndCopy(ss);
  }

  static INLINE SSID DoubletonSSID(SID sid1, SID sid2) {
    SegmentSet tmp(2);
    tmp.SetSID(0, sid1);
    tmp.SetSID(1, sid2);
    return FindExistingOrAlocateAndCopy(&tmp);
  }


  // testing only
  static SegmentSet *AddSegmentToTupleSS(SegmentSet *ss, SID new_sid) {
    SSID ssid = AddSegmentToTupleSS(ss->ComputeSSID(), new_sid);
    AssertLive(ssid, __LINE__);
    return Get(ssid);
  }
  static SegmentSet *Doubleton(SID sid1, SID sid2) {
    SSID ssid = DoubletonSSID(sid1, sid2);
    AssertLive(ssid, __LINE__);
    return Get(ssid);
  }



  // static data members
  template <int n>
  struct Less {
    INLINE bool operator () (const SegmentSet *ss1, const SegmentSet *ss2) const {
      DCHECK(ss1->size() == n);
      DCHECK(ss2->size() == n);
      for (int i = 0; i < n; i++) {
        SID sid1 = ss1->GetSID(i);
        SID sid2 = ss2->GetSID(i);
        if (sid1 != sid2) return sid1 < sid2;
      }
      return false;
    }
  };


  template <int n> 
  struct SSEq {
    INLINE bool operator () (const SegmentSet *ss1, const SegmentSet *ss2) const {
      DCHECK(ss1->size() == n);
      DCHECK(ss2->size() == n);
      if (n > 3 && ss1->GetSID(3) != ss2->GetSID(3)) return false;
      if (n > 2 && ss1->GetSID(2) != ss2->GetSID(2)) return false;
      return ss1->GetSID(0) == ss2->GetSID(0) && 
             ss1->GetSID(1) == ss2->GetSID(1);
    }
  };

  template <int n>
  struct SSHash {
    INLINE uintptr_t operator () (const SegmentSet *ss) const {
      uintptr_t res = ss->GetSID(0).raw() ^ ss->GetSID(1).raw();
      if (n == 2) return res;
      if (n == 3) return res ^ ss->GetSID(2).raw();
      if (n == 4) return res ^ ss->GetSID(2).raw() ^ ss->GetSID(3).raw();
      CHECK(0);
    }
  };

  template <class MapType>
  static SSID GetIdOrZeroFromMap(MapType &map, SegmentSet *ss) {
    typename MapType::iterator it = map.find(ss);
    if (it == map.end())
      return SSID(0);
    return it->second;
  }

  class Map {
   public:
    SSID GetIdOrZero(SegmentSet *ss) {
      int size = ss->size();
      switch(size) {
        case 2: return GetIdOrZeroFromMap(map2, ss);
        case 3: return GetIdOrZeroFromMap(map3, ss);
        case 4: return GetIdOrZeroFromMap(map4, ss);
        default: CHECK(0);
      }
    }

    void Insert(SegmentSet *ss, SSID id) {
      int size = ss->size();
      CheckSize(size);
      switch(size) {
        case 2: map2[ss] = id; break;
        case 3: map3[ss] = id; break;
        case 4: map4[ss] = id; break;
        default: CHECK(0);
      }
    }
    
    void Erase(SegmentSet *ss) {
      int size = ss->size();
      CheckSize(size);
      switch(size) {
        case 2: CHECK(map2.erase(ss)); break;
        case 3: CHECK(map3.erase(ss)); break;
        case 4: CHECK(map4.erase(ss)); break;
        default: CHECK(0);
      }
    }
  
    void Clear() {
      map2.clear();
      map3.clear();
      map4.clear();
    }

   private:
    void CheckSize(int size) { 
      DCHECK(size >= 2);
      DCHECK(size <= kMaxSegmentSetSize);
      DCHECK(kMaxSegmentSetSize == 4);
    }
 
#if 1
    // TODO(timurrrr): consider making a custom hash_table.
    typedef hash_map<SegmentSet*, SSID, SSHash<2>, SSEq<2> > Map2;
    typedef hash_map<SegmentSet*, SSID, SSHash<3>, SSEq<3> > Map3;
    typedef hash_map<SegmentSet*, SSID, SSHash<4>, SSEq<4> > Map4;
#else 
    typedef map<SegmentSet*, SSID, Less<2> > Map2;
    typedef map<SegmentSet*, SSID, Less<3> > Map3;
    typedef map<SegmentSet*, SSID, Less<4> > Map4;
#endif
    Map2 map2;
    Map3 map3;
    Map4 map4;
  };

//  typedef map<SegmentSet*, SSID, Less> Map;

  static Map                  *map_;
  static vector<SegmentSet *> *vec_; // TODO(kcc): use vector<SegmentSet> instead.
  static vector<SSID>         *reused_;


  SID     sids_[kMaxSegmentSetSize];
  int32_t size_; // TODO(kcc): consider deleting size_ at all.
  int32_t ref_count_;
};

SegmentSet::Map      *SegmentSet::map_;
vector<SegmentSet *> *SegmentSet::vec_;
vector<SSID>         *SegmentSet::reused_;




SSID SegmentSet::RemoveSegmentFromSS(SSID old_ssid, SID sid_to_remove) {
  DCHECK(old_ssid.IsValidOrEmpty());
  DCHECK(sid_to_remove.valid());

  if (old_ssid.IsEmpty()) return old_ssid;  // Nothing to remove.
  if (old_ssid.IsSingleton()) {
    SID sid = old_ssid.Singleton();
    if (Segment::HappensBeforeOrSameThread(sid, sid_to_remove)) 
      return SSID(0);  // Empty.
    return old_ssid;
  }
  return RemoveSegmentFromTupleSS(old_ssid, sid_to_remove);
}


// static
// TODO(timurrrr): describe this, see ThreadSanitizerAlgorithm names.
SSID SegmentSet::AddSegmentToSS(SSID old_ssid, SID new_sid) {
  // TODO: add caching
  DCHECK(new_sid.valid());
  SSID res;
  if (old_ssid.IsSingleton()) {
    res = AddSegmentToSingletonSS(old_ssid, new_sid);
  } else if (old_ssid.IsEmpty()) {
    res = SSID(new_sid);
  } else {
    res = AddSegmentToTupleSS(old_ssid, new_sid);
  }
  SegmentSet::AssertLive(res, __LINE__);
  return res;
}

// static
SSID SegmentSet::AddSegmentToSingletonSS(SSID ssid, SID new_sid) {
  CHECK(ssid.IsSingleton());
  CHECK(ssid.valid());
  SID old_sid(ssid.raw());
  if (old_sid == new_sid) {  // Same segment. 
    return ssid;
  }
  CHECK(old_sid.valid());
  Segment::AssertLive(old_sid, __LINE__);
  Segment::AssertLive(new_sid, __LINE__);
  TID old_tid = Segment::Get(old_sid)->tid();
  TID new_tid = Segment::Get(new_sid)->tid();
  if (old_tid == new_tid) {  // Same thread.
    return SSID(new_sid.raw());
  }
  if (Segment::HappensBefore(old_sid, new_sid)) {
    // Another thread, but old segment happens before new one.
    return SSID(new_sid.raw());
  }
  // another segment, no happens-before relation.
  return old_tid < new_tid 
      ? DoubletonSSID(old_sid, new_sid)
      : DoubletonSSID(new_sid, old_sid);
}

SSID SegmentSet::RemoveSegmentFromTupleSS(SSID ssid, SID sid_to_remove) {
  DCHECK(ssid.IsTuple());
  DCHECK(ssid.valid());
  AssertLive(ssid, __LINE__);
  SegmentSet *ss = Get(ssid);
  int32_t old_size = ss->size();
  CHECK(old_size <= kMaxSegmentSetSize);
  

  int32_t new_size = 0;
  SID tmp_sids[kMaxSegmentSetSize];
  CHECK(sizeof(int32_t) == sizeof(SID));

  for (int i = 0; i < old_size; i++) {
    SID sid = ss->GetSID(i);
    DCHECK(sid.valid());
    Segment::AssertLive(sid, __LINE__);
    if (Segment::HappensBeforeOrSameThread(sid, sid_to_remove))
      continue;  // Skip this segment from the result.
    tmp_sids[new_size++] = sid;
  }

  if (new_size == 0) return SSID(0); 
  if (new_size == 1) return SSID(tmp_sids[0]);
  if (new_size == old_size) return ssid;

  SegmentSet tmp(new_size);
  for (int i = 0; i < new_size; i++)
    tmp.sids_[i] = tmp_sids[i];
  if (DEBUG_MODE) tmp.Validate(__LINE__);

  SSID res = FindExistingOrAlocateAndCopy(&tmp);
  if (DEBUG_MODE) Get(res)->Validate(__LINE__);
  return res;
}

//  static
SSID SegmentSet::AddSegmentToTupleSS(SSID ssid, SID new_sid) {
  DCHECK(ssid.IsTuple());
  DCHECK(ssid.valid());
  AssertLive(ssid, __LINE__);
  SegmentSet *ss = Get(ssid);
  int32_t old_size = ss->size();
  CHECK(old_size <= kMaxSegmentSetSize);

  Segment::AssertLive(new_sid, __LINE__);
  const Segment *new_seg = Segment::Get(new_sid);
  TID            new_tid = new_seg->tid();

  int32_t new_size = 0;
  SID tmp_sids[kMaxSegmentSetSize + 1];
  CHECK(sizeof(int32_t) == sizeof(SID));
  bool inserted_new_sid = false;
  // traverse all SID in current ss. tids are ordered.
  for (int i = 0; i < old_size; i++) {
    SID            sid = ss->GetSID(i);
    DCHECK(sid.valid());
    Segment::AssertLive(sid, __LINE__);
    const Segment *seg = Segment::Get(sid);
    TID            tid = seg->tid();

    if (sid == new_sid) {
      // we are trying to insert a sid which is already there. 
      // SS will not change.
      return ssid;
    }

    if (tid == new_tid) {
      // we have another segment from the same thread => replace it.
      tmp_sids[new_size++] = new_sid;
      inserted_new_sid = true;
      continue;
    }

    if (tid > new_tid && !inserted_new_sid) {
      // there was no segment with this tid, put it now.
      tmp_sids[new_size++] = new_sid;
      inserted_new_sid = true;
    }

    if (!Segment::HappensBefore(sid, new_sid)) {
      DCHECK(!Segment::HappensBefore(new_sid, sid));
      tmp_sids[new_size++] = sid;
    }
  }

  if (!inserted_new_sid) {
    tmp_sids[new_size++] = new_sid;
  }

  CHECK(new_size > 0);
  if (new_size == 1) {
    return SSID(new_sid.raw());  // Singleton.
  }

  if (new_size > kMaxSegmentSetSize) {
    CHECK(new_size == kMaxSegmentSetSize + 1);
    // we need to forget one segment. Which? Random, but not the new one.
    int seg_to_forget = rand() % kMaxSegmentSetSize;

    if (tmp_sids[seg_to_forget] == new_sid) {
      seg_to_forget++;
      if (seg_to_forget == kMaxSegmentSetSize) 
        seg_to_forget = 0;
    }
    for (int i = seg_to_forget; i < new_size - 1; i++) {
      tmp_sids[i] = tmp_sids[i+1];
    }
    new_size--;
  }

  CHECK(new_size <= kMaxSegmentSetSize);
  SegmentSet tmp(new_size);
  for (int i = 0; i < new_size; i++)
    tmp.sids_[i] = tmp_sids[i];
  if (DEBUG_MODE) tmp.Validate(__LINE__);

  SSID res = FindExistingOrAlocateAndCopy(&tmp);
  if (DEBUG_MODE) Get(res)->Validate(__LINE__);
  return res;
}
 


void NOINLINE SegmentSet::Validate(int line) const {
  // This is expensive! 
  for (int i = 0; i < size(); i++) {
    for (int j = i + 1; j < size(); j++){
      SID sid1 = GetSID(i);
      SID sid2 = GetSID(j);
      CHECK(sid1.valid());
      CHECK(sid2.valid());
      Segment::AssertLive(sid1, __LINE__);
      Segment::AssertLive(sid2, __LINE__);
      bool hb1 = Segment::HappensBefore(sid1, sid2);
      bool hb2 = Segment::HappensBefore(sid2, sid1);
      if (hb1 || hb2) {
        Printf("BAD at line %d: %d %d %s %s\n   %s\n   %s\n", line, (int)hb1, (int)hb2,
               Segment::ToString(sid1).c_str(),
               Segment::ToString(sid2).c_str(),
               Segment::Get(sid1)->vts()->ToString().c_str(),
               Segment::Get(sid2)->vts()->ToString().c_str()
              );
      }
      CHECK(!Segment::HappensBefore(GetSID(i), GetSID(j)));
      CHECK(!Segment::HappensBefore(GetSID(j), GetSID(i)));
      CHECK(Segment::Get(sid1)->tid() < Segment::Get(sid2)->tid());
    }
  }
}

string SegmentSet::ToStringWithLocks(SSID ssid) {
  if (ssid.IsEmpty()) return "";
  string res = "";
  for (int i = 0; i < Size(ssid); i++) {
    SID sid = GetSID(ssid, i, __LINE__);
    if (i) res += ", ";
    res += Segment::ToStringWithLocks(sid);
  }
  return res;
}

string SegmentSet::ToString() const {
  Validate(__LINE__);
  string res = "{";
  for (int i = 0; i < size(); i++) {
    if (i) res += ", ";
    CHECK(GetSID(i).valid());
    Segment::AssertLive(GetSID(i), __LINE__);
    res += Segment::ToStringTidOnly(GetSID(i)).c_str();
  }
  res += "}";
  return res;
}

// static 
void SegmentSet::Test() {
  LSID ls(0); // dummy
  SID sid1 = Segment::AddNewSegment(TID(0), VTS::Parse("[0:2;]"), ls, ls);
  SID sid2 = Segment::AddNewSegment(TID(1), VTS::Parse("[0:1; 1:1]"), ls, ls);
  SID sid3 = Segment::AddNewSegment(TID(2), VTS::Parse("[0:1; 2:1]"), ls, ls);
  SID sid4 = Segment::AddNewSegment(TID(3), VTS::Parse("[0:1; 3:1]"), ls, ls);
  SID sid5 = Segment::AddNewSegment(TID(4), VTS::Parse("[0:3; 2:2; 3:2;]"), ls, ls);
  SID sid6 = Segment::AddNewSegment(TID(4), VTS::Parse("[0:3; 1:2; 2:2; 3:2;]"), ls, ls);


  // SS1:{T0/S1, T2/S3}
  SegmentSet *d1 = SegmentSet::Doubleton(sid1, sid3);
  d1->Print();
  CHECK(SegmentSet::Doubleton(sid1, sid3) == d1);
  // SS2:{T0/S1, T1/S2, T2/S3}
  SegmentSet *d2 = SegmentSet::AddSegmentToTupleSS(d1, sid2);
  CHECK(SegmentSet::AddSegmentToTupleSS(d1, sid2) == d2);
  d2->Print();

  // SS3:{T0/S1, T2/S3, T3/S4}
  SegmentSet *d3 = SegmentSet::AddSegmentToTupleSS(d1, sid4);
  CHECK(SegmentSet::AddSegmentToTupleSS(d1, sid4) == d3);
  d3->Print();

  // SS4:{T0/S1, T1/S2, T2/S3, T3/S4}
  SegmentSet *d4 = SegmentSet::AddSegmentToTupleSS(d2, sid4);
  CHECK(SegmentSet::AddSegmentToTupleSS(d2, sid4) == d4);
  CHECK(SegmentSet::AddSegmentToTupleSS(d3, sid2) == d4);
  d4->Print();

  // SS5:{T1/S2, T4/S5}
  SegmentSet *d5 = SegmentSet::AddSegmentToTupleSS(d4, sid5);
  d5->Print();

  SSID ssid6 = SegmentSet::AddSegmentToTupleSS(d4->ComputeSSID(), sid6);
  CHECK(ssid6.IsSingleton());
  Printf("%s\n", ToString(ssid6).c_str());
  CHECK(sid6.raw() == 6);
  CHECK(ssid6.raw() == 6);
}

//--------- Shadow Value ------------ {{{1
class ShadowValue {
 public:
  ShadowValue() {
    rd_ssid_ = 0xDEADBEEF;
    wr_ssid_ = 0xDEADBEEF;
  }  

  void Clear() { 
    rd_ssid_ = 0;
    wr_ssid_ = 0;
  }

  bool IsNew() const { return rd_ssid_ == 0 && wr_ssid_ == 0; }
  // new experimental state machine.
  SSID rd_ssid() const { return SSID(rd_ssid_); }
  SSID wr_ssid() const { return SSID(wr_ssid_); }
  void set(SSID rd_ssid, SSID wr_ssid) {
    rd_ssid_ = rd_ssid.raw();
    wr_ssid_ = wr_ssid.raw();
  }

  // comparison 
  bool operator == (const ShadowValue &sval) const { 
    return rd_ssid_ == sval.rd_ssid_ && 
        wr_ssid_ == sval.wr_ssid_;
  }
  bool operator != (const ShadowValue &sval) const { 
    return !(*this == sval);
  }

  string ToString() const {
    char buff[1000];
    if (IsNew()) {
      return "{New}";
    }
    sprintf(buff, "Reads: %s; Writes: %s", 
            SegmentSet::ToStringWithLocks(rd_ssid()).c_str(),
            SegmentSet::ToStringWithLocks(wr_ssid()).c_str()
           );
    return buff;
  }

 private:
  int32_t rd_ssid_;
  int32_t wr_ssid_;
};

//--------- CacheLine --------------- {{{1
class CacheLineBase {
 public:
  static const uintptr_t kLineSizeBits = 6;  // Don't change this.
  static const uintptr_t kLineSize = 1 << kLineSizeBits;
 protected:
  uintptr_t tag_;
  TID creator_tid_;
  bool is_compressed_;
  Mask used_;
  Mask traced_;
  Mask racey_;
  Mask published_;
};

// Uncompressed line. Just a vector of 64 shadow values. 
class CacheLineUncompressed : public CacheLineBase {
 protected:
  ShadowValue vals_[kLineSize];
};

// Compressed line. Few shadow values and their positions.
class CacheLineCompressed : public CacheLineBase {
 public:
  static const uintptr_t kCompressedLineSize = 10;
  uintptr_t   n_vals;
  ShadowValue vals[kCompressedLineSize];
  Mask        positions[kCompressedLineSize];
};

class CacheLine : public CacheLineUncompressed {
 public:

  static CacheLine *CreateNewCacheLine(uintptr_t tag) {
    ScopedMallocCostCenter cc("CreateNewCacheLine");
    void *mem = free_list_->Allocate();
    DCHECK(mem);
    return new (mem) CacheLine(tag);
  }

  static void Delete(CacheLine *line) {
    free_list_->Deallocate(line);
  }

  Mask &used()   { return used_;  }
  Mask &traced() { return traced_; }
  Mask &published() { return published_; } 
  Mask &racey()  { return racey_; }
  uint64_t tag() { return tag_; } 

  TID creator_tid() const { return creator_tid_; } 
  void set_creator_tid(TID tid) { creator_tid_ = tid; }


  Mask ClearRangeAndReturnOld(uint64_t from, uint64_t to) {
    traced_.ClearRange(from, to);
    published_.ClearRange(from, to);
    racey_.ClearRange(from, to);
    return used_.ClearRangeAndReturnOld(from, to);
  }

  void Clear() {
    used_.Clear();
    traced_.Clear();
    published_.Clear();
    racey_.Clear();
  }

  ShadowValue *GetValuePointer(uintptr_t offset) {
    DCHECK(offset < kLineSize);
    return  &vals_[offset];
  }
  ShadowValue  GetValue(uintptr_t offset) { return *GetValuePointer(offset); }

  static uintptr_t ComputeOffset(uintptr_t a) { 
    return a & (kLineSize - 1); 
  }
  static uintptr_t ComputeTag(uintptr_t a) { 
    return a & ~(kLineSize - 1);
  }
  static uintptr_t ComputeNextTag(uintptr_t a) {
    return ComputeTag(a) + kLineSize;
  }

  // TODO(timurrrr): add a comment on how this actually works.
  bool SameValueStored(uintptr_t addr, uintptr_t size) {
    uintptr_t off = ComputeOffset(addr);
    if (off & (size - 1)) return false; // Not aligned.
    DCHECK(off + size <= kLineSize);
    DCHECK(size == 2 || size == 4 || size == 8);
    if (used_.GetRange(off + 1, off + size) == 0) 
      return true;
    return false;
  }

  static CacheLine *Compress(CacheLine *line);
  static CacheLine *Uncompress(CacheLine *line);

  static void InitClassMembers() {
    if (DEBUG_MODE) {
      Printf("sizeof(CacheLine) = %ld\n", sizeof(CacheLine));
    }
    free_list_ = new FreeList(sizeof(CacheLine), 1024);
  }

 private:

  CacheLine(uint64_t tag) { 
    tag_ = tag;
    is_compressed_ = false;
  }
  ~CacheLine() { }

  // no non-static data members.

  static FreeList *free_list_;
};

FreeList *CacheLine::free_list_;


//static 
CacheLine *CacheLine::Compress(CacheLine *line) {
  if (!G_flags->compress_cache_lines) {
    return line;
  }
  CHECK(0);
  return NULL;
}
// static 
CacheLine *CacheLine::Uncompress(CacheLine *line) {
  if (!G_flags->compress_cache_lines) {
    DCHECK(!line->is_compressed_);
    return line;
  }
  if (!line->is_compressed_) {
    return line;
  }
  CHECK(0);
  return NULL;
}


// If range [a,b) fits into one line, return that line's tag.
// Else range [a,b) belongs is broken into these ranges: 
//   [a, line1_tag)
//   [line1_tag, line2_tag)
//   [line2_tag, b)
uintptr_t GetCacheLinesForRange(uintptr_t a, uintptr_t b, 
                                     uintptr_t *line1_tag, uintptr_t *line2_tag) {
  uintptr_t a_tag = CacheLine::ComputeTag(a);
  uintptr_t next_tag = CacheLine::ComputeNextTag(a);
  if (b < next_tag) {
    return a_tag;   
  }
  *line1_tag = next_tag;
  *line2_tag = CacheLine::ComputeTag(b);
  return 0;
}


//--------- Cache ------------------ {{{1
class Cache {
 public:
  Cache() {
    ForgetAllState();
  } 

  // HOT
  INLINE CacheLine *GetLine(uintptr_t a, int call_site) {
    uintptr_t tag = CacheLine::ComputeTag(a);
    DCHECK(tag <= a);
    DCHECK(tag + CacheLine::kLineSize > a);
    uintptr_t cli = ComputeCacheLineIndexInCache(a);
    CacheLine *res = lines_[cli];
    if (LIKELY(res && res->tag() == tag)) {
      G_stats->cache_fast_get++;
    } else {
      res = WriteBackAndFetch(tag, cli);
    }
    return res;
  }

  void ForgetAllState() {
    for (int i = 0; i < kNumLines; i++) {
      lines_[i] = NULL;
    }
    for (Map::iterator i = storage_.begin(); i != storage_.end(); ++i) {
      CacheLine *line = i->second;
      CacheLine::Delete(line);
    }
    storage_.clear();
  }

  void PrintStorageStats() {
    if (!G_flags->show_stats) return;
    map<size_t, int> sizes;
    for (Map::iterator it = storage_.begin(); it != storage_.end(); ++it) {
      CacheLine *line = it->second;
      uintptr_t cli = ComputeCacheLineIndexInCache(line->tag());
      if (lines_[cli] == line) {
        // this line is in cache -- ignore it.
        continue;
      }
      set<int64_t> s;
      for (int i = 0; i < 64; i++) {
        if (line->used().Get(i)) {
          int64_t sval = *(int64_t*)line->GetValuePointer(i);
          s.insert(sval);
        }
      }
      sizes[s.size()]++;
    }
    Printf("Storage sizes: %ld\n", storage_.size());
    for (size_t size = 0; size <= 64; size++) {
      if (sizes[size]) {
        Printf("  %ld => %d\n", size, sizes[size]);
      }
    }
  }

 private:
  INLINE uintptr_t ComputeCacheLineIndexInCache(uintptr_t addr) {
    return (addr >> CacheLine::kLineSizeBits) & (kNumLines - 1);
  }

  NOINLINE CacheLine *WriteBackAndFetch(uintptr_t tag, uintptr_t cli) {
    ScopedMallocCostCenter cc("Cache::WriteBackAndFetch");
    CacheLine *res;
    size_t old_storage_size = storage_.size();
    CacheLine **line_for_this_tag = &storage_[tag];
    CacheLine *old_line = lines_[cli];
    if (*line_for_this_tag == NULL) {
      // creating a new cache line
      CHECK(storage_.size() == old_storage_size + 1);
      res = CacheLine::CreateNewCacheLine(tag);
      *line_for_this_tag = res;
      lines_[cli]        = res;
      G_stats->cache_new_line++;
    } else {
      // taking an existing cache line from storage.
      res = *line_for_this_tag;
      DCHECK(!res->used().Empty());
      lines_[cli]        = res;
      G_stats->cache_fetch++;
    }


    if (old_line) {
      if (old_line->used().Empty()) {
        storage_.erase(old_line->tag());
        CacheLine::Delete(old_line);
        G_stats->cache_delete_empty_line++;
      } else {
        if (DEBUG_MODE && 0) {
          DebugOnlyCheckCacheLineWhichWeReplace(old_line);
        }
      }
    }
    DCHECK(res->tag() == tag);

    if (G_stats->cache_max_storage_size < storage_.size()) {
      G_stats->cache_max_storage_size = storage_.size();
      // if ((storage_.size() % (1024 * 64)) == 0) {
      //  PrintStorageStats();
      // }
    }

    return res;
  }

  void DebugOnlyCheckCacheLineWhichWeReplace(CacheLine *old_line) {
    static int c = 0;
    c++;
    if ((c % 1024) == 1) {
      set<int64_t> s;
      for (int i = 0; i < 64; i++) {
        if (old_line->used().Get(i)) {
          int64_t sval = *(int64_t*)old_line->GetValuePointer(i);
          // Printf("%p ", sval);
          s.insert(sval);
        }
      }
      Printf("\n[%d] Cache Size=%ld %s different values: %ld\n", c,
             storage_.size(), old_line->used().ToString().c_str(), 
             s.size());
      G_stats->PrintStatsForCache();
    }
  }

  static const int kNumLines = 1 << 16;
  CacheLine *lines_[kNumLines];

  // tag => CacheLine
  typedef map<uintptr_t, CacheLine*> Map;  // TODO: is hash_map better than map?
  Map storage_;
};

static  Cache *G_cache;

//--------- Published range -------------------- {{{1
struct PublishInfo {
  uintptr_t tag;    ///< Tag of the cache line where the mem is published.
  Mask      mask;  ///< The bits that are actually published.
  VTS      *vts;    ///< The point where this range has been published.
};


typedef multimap<uintptr_t, PublishInfo> PublishInfoMap; 

/// Maps 'mem+size' to the PublishInfoMap{mem, size, vts}.
static PublishInfoMap *g_publish_info_map;

const int kDebugPublish = 0;

// Get a VTS where 'a' has been published, 
// return NULL if 'a' was not published. 
static const VTS *GetPublisherVTS(uintptr_t a) {
  uintptr_t tag = CacheLine::ComputeTag(a);
  uintptr_t off = CacheLine::ComputeOffset(a);
  typedef PublishInfoMap::iterator Iter;

  pair<Iter, Iter> eq_range = g_publish_info_map->equal_range(tag);
  for (Iter it = eq_range.first; it != eq_range.second; ++it) {
    PublishInfo &info = it->second;
    DCHECK(info.tag == tag);
    if (info.mask.Get(off)) {
      G_stats->publish_get++;
      // Printf("GetPublisherVTS: a=%p vts=%p\n", a, info.vts);
      return info.vts;
    }
  }
  Printf("GetPublisherVTS returned NULL: a=%p\n", a);
  return NULL;
}

static bool CheckSanityOfPublishedMemory(uintptr_t tag, int line) {
  if (!DEBUG_MODE) return true;
  if (kDebugPublish)
    Printf("CheckSanityOfPublishedMemory: line=%d\n", line);
  typedef PublishInfoMap::iterator Iter;
  pair<Iter, Iter> eq_range = g_publish_info_map->equal_range(tag);
  Mask union_of_masks(0);
  // iterate over all entries for this tag
  for (Iter it = eq_range.first; it != eq_range.second; ++it) {
    PublishInfo &info = it->second;
    CHECK(info.tag  == tag);
    CHECK(it->first == tag);
    CHECK(info.vts);
    Mask mask(info.mask);
    CHECK(!mask.Empty());  // Mask should not be empty..
    // And should not intersect with other masks.
    CHECK(Mask::Intersection(union_of_masks, mask).Empty());
    union_of_masks.Union(mask);
  }
  return true;
}

// Un-publish bytes from 'line' that are set in 'mask'
static void UnpublishMemory(CacheLine *line, Mask mask) {
  CHECK(CheckSanityOfPublishedMemory(line->tag(), __LINE__));
  typedef PublishInfoMap::iterator Iter;
  bool deleted_some = true;
  if (kDebugPublish)
    Printf(" UnpublishRange: %p %s\n", 
           line->tag(), mask.ToString().c_str());
  while (deleted_some) {
    deleted_some = false;
    pair<Iter, Iter> eq_range = g_publish_info_map->equal_range(line->tag());
    for (Iter it = eq_range.first; it != eq_range.second; ++it) {
      PublishInfo &info = it->second;
      DCHECK(info.tag == line->tag());
      if (kDebugPublish)
        Printf("?UnpublishRange: %p %s\n", line->tag(), info.mask.ToString().c_str());
      info.mask.Subtract(mask);
      if (kDebugPublish)
        Printf("+UnpublishRange: %p %s\n", line->tag(), info.mask.ToString().c_str());
      G_stats->publish_clear++;
      if (info.mask.Empty()) {
        VTS::Delete(info.vts);
        g_publish_info_map->erase(it);
        deleted_some = true;
        break;
      }
    }
  }
  CHECK(CheckSanityOfPublishedMemory(line->tag(), __LINE__));
}

// Publish range [a, b) in addr's CacheLine with vts.
static void PublishRangeInOneLine(uintptr_t addr, uintptr_t a, 
                                  uintptr_t b, VTS *vts) {
  ScopedMallocCostCenter cc("PublishRangeInOneLine");
  DCHECK(addr);
  DCHECK(b <= CacheLine::kLineSize);
  DCHECK(a < b);
  uintptr_t tag = CacheLine::ComputeTag(addr);
  CHECK(CheckSanityOfPublishedMemory(tag, __LINE__));
  CacheLine *line = G_cache->GetLine(tag, __LINE__);

  if (1 || line->published().GetRange(a,b)) {
    Mask mask(0);
    mask.SetRange(a,b);
    // TODO(timurrrr): add warning for re-publishing.
    UnpublishMemory(line, mask);
  }

  line->published().SetRange(a, b);

  PublishInfo pub_info;
  pub_info.tag  = tag;
  pub_info.mask.SetRange(a, b);
  pub_info.vts  = vts;
  g_publish_info_map->insert(make_pair(tag, pub_info));
  G_stats->publish_set++;
  if (kDebugPublish)
    Printf("PublishRange   : %p %s vts=%p\n", 
           tag, pub_info.mask.ToString().c_str(), vts);
  CHECK(CheckSanityOfPublishedMemory(tag, __LINE__));
}

// Publish memory range [a, b).
static void PublishRange(uintptr_t a, uintptr_t b, VTS *vts) {
  uintptr_t line1_tag = 0, line2_tag = 0;
  uintptr_t tag = GetCacheLinesForRange(a, b, &line1_tag, &line2_tag);
  if (tag) {
    PublishRangeInOneLine(tag, a - tag, b - tag, vts);
    return;
  }
  uintptr_t a_tag = CacheLine::ComputeTag(a);
  PublishRangeInOneLine(a, a - a_tag, CacheLine::kLineSize, vts);
  for (uintptr_t tag_i = line1_tag; tag_i < line2_tag; 
       tag_i += CacheLine::kLineSize) {
    PublishRangeInOneLine(tag_i, 0, CacheLine::kLineSize, 
                          vts->Clone());
  }
  if (b > line2_tag) {
    PublishRangeInOneLine(line2_tag, 0, b - line2_tag, vts->Clone());
  }
}




//--------- Clear Memory State ------------------ {{{1

static void INLINE UnrefSegmentsInMemoryRange(uintptr_t a, uintptr_t b, 
                                                Mask mask, CacheLine *line) {
  DCHECK(!mask.Empty());
  for (uintptr_t x = a; x < b; x++) { // slow?
    if (!mask.Get(x)) continue;
    ShadowValue sval = line->GetValue(x);
    SSID rd_ssid = sval.rd_ssid();
    if (!rd_ssid.IsEmpty()) {
      DCHECK(rd_ssid.valid());
      SegmentSet::Unref(rd_ssid, "Detector::UnrefSegmentsInMemoryRange");
    }
    SSID wr_ssid = sval.wr_ssid();
    if (!wr_ssid.IsEmpty()) {
      DCHECK(wr_ssid.valid());
      SegmentSet::Unref(wr_ssid, "Detector::UnrefSegmentsInMemoryRange");
    }
  }
}


void INLINE ClearMemoryStateInOneLine(uintptr_t addr,
                                      uintptr_t beg, uintptr_t end, 
                                      bool is_new_mem) {
  CacheLine *line = G_cache->GetLine(addr, __LINE__);
  DCHECK(line);
  DCHECK(beg < CacheLine::kLineSize);
  DCHECK(end <= CacheLine::kLineSize);
  DCHECK(beg < end);
  Mask published = line->published();
  if (UNLIKELY(!published.Empty())) {
    Mask mask(published.GetRange(beg, end));
    UnpublishMemory(line, mask);
  }
  Mask old_used = line->ClearRangeAndReturnOld(beg, end);
  if (UNLIKELY(!is_new_mem && !old_used.Empty())) {
    UnrefSegmentsInMemoryRange(beg, end, old_used, line);
  } 
  if (line->used().Empty()) {
    line->set_creator_tid(TID());
  }
}



// clear memory state for [a,b)
void INLINE ClearMemoryState(uintptr_t a, uintptr_t b, bool is_new_mem) {
  DCHECK(a < b);
  if (a == b) return;
  uintptr_t line1_tag = 0, line2_tag = 0;
  uintptr_t single_line_tag = GetCacheLinesForRange(a, b, 
                                                    &line1_tag, &line2_tag);
  if (single_line_tag) {
    ClearMemoryStateInOneLine(a, a - single_line_tag, 
                              b - single_line_tag, is_new_mem);
    return;
  }

  uintptr_t a_tag = CacheLine::ComputeTag(a);
  ClearMemoryStateInOneLine(a, a - a_tag, CacheLine::kLineSize, is_new_mem);

  for (uintptr_t tag_i = line1_tag; tag_i < line2_tag; 
       tag_i += CacheLine::kLineSize) {
    ClearMemoryStateInOneLine(tag_i, 0, CacheLine::kLineSize, is_new_mem);
  }

  if (b > line2_tag) {
    ClearMemoryStateInOneLine(line2_tag, 0, b - line2_tag, is_new_mem);
  }

  if (DEBUG_MODE && G_flags->debug_level >= 2) {  // check that we've cleared it. Slow! 
    for (uintptr_t x = a; x < b; x++) {
      uintptr_t off = CacheLine::ComputeOffset(x);
      CacheLine *line = G_cache->GetLine(x, __LINE__);
      CHECK(!line->used().Get(off));
    }
  }
}



//--------- Thread ------------------ {{{1
struct Thread {
 public:
  Thread(TID tid, TID parent_tid, VTS *vts, StackTrace *creation_context) 
    : is_running_(true),
      tid_(tid), 
      sid_(0), 
      parent_tid_(parent_tid),
      max_sp_(0),
      min_sp_(-1),
      cur_sp_(0),
      new_sp_(0),
      creation_context_(creation_context),
      announced_(false),
      join_child_ptid_(0),
      rd_lockset_(0),
      wr_lockset_(0),
      lock_mu_(0),
      wait_cv_(0),
      wait_mu_(0),
      wait_cv_and_mu_set_(false),
      bus_lock_is_set_(false), 
      vts_at_exit_(NULL) {

    NewSegmentWithoutUnrefingOld("Thread Creation", vts);

    call_stack_.reserve(100);
    HandleRtnCall(0, 0);
    ignore_[0] = ignore_[1] = 0;
    ignore_context_[0] = ignore_context_[1] = NULL;
    if (tid != TID(0)) {
      CHECK(creation_context_);
    }
    Add(this);
  }

  TID tid() const { return tid_; }
  TID parent_tid() const { return parent_tid_; }

  // STACK 
  uintptr_t max_sp() const { return max_sp_; }
  uintptr_t min_sp() const { return min_sp_; }
  uintptr_t cur_sp() const { return cur_sp_; }
  uintptr_t new_sp() const { return new_sp_; }
  void set_cur_sp(uintptr_t sp)   { cur_sp_ = sp;}
  void set_new_sp(uintptr_t sp)   { new_sp_ = sp;}

  bool Announce() {
    if (announced_) return false;
    announced_ = true;
    if (tid_ == TID(0)) {
      Report("INFO: T0 is program's main thread\n");
    } else {

      if (G_flags->announce_threads) {
        Report("INFO: T%d has been created by T%d at this point: {{{\n%s}}}\n", 
               tid_.raw(), parent_tid_.raw(), 
               creation_context_->ToString().c_str());
        Thread * parent = GetIfExists(parent_tid_);
        CHECK(parent);
        parent->Announce();
      } else {
        Report("INFO: T%d has been created by T%d. "
               "Use --announce-threads to see the creation stack.\n",
               tid_.raw(), parent_tid_.raw());
      }
    }
    return true;
  }

  string ThreadName() const {
    char buff[100];
    sprintf(buff, "T%d", tid().raw());
    string res = buff;
    if (thread_name_.length() > 0) {
      res += " (";
      res += thread_name_;
      res += ")";
    }
    return res;
  }

  bool MemoryIsInStack (uintptr_t a) {
    return a >= min_sp_ && a <= max_sp_;
  }

  // TODO: how to compute this?? 
  static uintptr_t ThreadStackSize() {
    return 8 * 1024 * 1024;
  }

  bool is_running() const { return is_running_; }

  // ignore
  void set_ignore(bool is_w, bool on) {
    ignore_[is_w] += on ? 1 : -1;
    if (on && G_flags->debug_level >= 1) {
      StackTrace::Delete(ignore_context_[is_w]);
      ignore_context_[is_w] = CreateStackTrace(0, 3);
    }
  }
  bool ignore(bool is_w) { return ignore_[is_w]; }

  StackTrace *GetLastIgnoreContext(bool is_w) {
    return ignore_context_[is_w];
  }

  SID sid() const {
    return sid_;
  }

  Segment *segment() const {
    CHECK(sid().valid());
    Segment::AssertLive(sid(), __LINE__);
    return Segment::Get(sid());
  }

  VTS *vts() const {
    return segment()->vts();
  }

  static void SetThreadPthreadT(TID tid, pthread_t ptid) {
    CHECK(tid.IsValid());
    CHECK(ptid);

    (*ptid_to_tid_)[ptid] = tid;
    if (0) {
      Printf("T%d: pthread_t=%p\n", tid.raw(), ptid);
    }
  }

  void set_thread_name(const char *name) {
    thread_name_ = string(name);
  }
  
  void HandleThreadEnd() {
    is_running_ = false;
    vts_at_exit_ = vts()->Clone();
  }

  void HandleThreadJoinBefore(pthread_t join_child_ptid) {
    CHECK(join_child_ptid_ == 0);
    CHECK(join_child_ptid != 0);
    join_child_ptid_ = join_child_ptid;
  }

  // Return the TID of the joined child and it's vts
  TID HandleThreadJoinAfter(VTS **vts_at_exit) {
    if (0) {
      Printf("T%d: Joined child : pthread_t=%p size=%ld\n", 
             tid().raw(), join_child_ptid_, ptid_to_tid_->size());  
    }
    CHECK(join_child_ptid_ != 0);
    CHECK(ptid_to_tid_->count(join_child_ptid_));
    TID child_tid = (*ptid_to_tid_)[join_child_ptid_];
    ptid_to_tid_->erase(join_child_ptid_);
    join_child_ptid_ = 0;
    *vts_at_exit = Thread::Get(child_tid)->vts_at_exit_;
    if (0)
    Printf("T%d: vts_at_exit_: %s\n", child_tid.raw(), 
           (*vts_at_exit)->ToString().c_str());
    return child_tid;
  }



  static int NumberOfThreads() { return all_threads_->size(); }

  static Thread *GetIfExists(TID tid) {
    if (tid.raw() < NumberOfThreads())
      return Get(tid);
    return NULL;
  }

  static Thread *Get(TID tid) {
    CHECK(tid.raw() < NumberOfThreads());
    return (*all_threads_)[tid.raw()];
  } 

  static TID Add(Thread *thr) {
    // Printf("Thread::Add %d %d\n", thr->tid().raw(), (int)all_threads_->size());
    TID tid = thr->tid();
    while(tid.raw() > (int32_t)all_threads_->size()) {
      all_threads_->push_back(NULL);
    }
    if (tid.raw() == (int32_t)all_threads_->size()){
      all_threads_->push_back(thr);
    } else {
      CHECK(tid.raw() < (int32_t)all_threads_->size());
      CHECK((*all_threads_)[tid.raw()] == NULL);
      (*all_threads_)[tid.raw()] = thr;
    }
    return thr->tid();
  }



  // Locks
  
  void set_bus_lock_is_set(bool is_set) { bus_lock_is_set_ = is_set; }
  bool bus_lock_is_set() const { return bus_lock_is_set_; } 
  
  void HandleLockBefore(uintptr_t mu) {
    lock_mu_ = mu;
  }
  void HandleLock(bool is_w_lock) {
    CHECK(lock_mu_);
    HandleLock(lock_mu_, is_w_lock);
    lock_mu_ = 0;
  }
  void HandleLock(uintptr_t lock_addr, bool is_w_lock) {
    Lock *lock = Lock::LookupOrCreate(lock_addr);
    // NOTE: we assume that all locks can be acquired recurively.
    // No warning about recursive locking will be issued. 
    if (is_w_lock) {
      // Recursive locks are properly handled because LockSet is in fact a
      // multiset.
      wr_lockset_ = LockSet::Add(wr_lockset_, lock);
      rd_lockset_ = LockSet::Add(rd_lockset_, lock);
      lock->WrLock(CreateStackTrace());
    } else {
      if (lock->wr_held()) {
        ReportStackTrace();
      }
      rd_lockset_ = LockSet::Add(rd_lockset_, lock);
      lock->RdLock(CreateStackTrace());
    }

    if (G_flags->pure_happens_before || lock->is_pure_happens_before()) {
      HandleWait(lock->lock_addr(), NULL, false);
    }

    NewSegmentForLockingEvent();

    if (G_flags->verbosity >= 2)
      Printf("T%d %sLock   %p; %s\n", 
           tid_.raw(), 
           is_w_lock ? "Wr" : "Rd", 
           lock_addr, 
           LockSet::ToString(lsid(is_w_lock)).c_str());
  }

  void HandleUnlock(uintptr_t lock_addr) {
    Lock *lock = Lock::Lookup(lock_addr);
    CHECK(lock);

    bool is_w_lock = lock->wr_held();

    if (G_flags->pure_happens_before || lock->is_pure_happens_before()) {
      HandleSignal(lock->lock_addr());
      if (!is_w_lock) {
      }
    }

    bool removed = false;

    if (is_w_lock) {
      lock->WrUnlock();
      removed =  LockSet::Remove(wr_lockset_, lock, &wr_lockset_)
              && LockSet::Remove(rd_lockset_, lock, &rd_lockset_);
    } else {
      lock->RdUnlock();
      removed = LockSet::Remove(rd_lockset_, lock, &rd_lockset_);
    }

    if (!removed) {
      // TODO(kcc): do we need to issue a real valgrind-ish warning?
      Report("WARNING: Lock %s was released by thread T%d"
             " which did not acquire this lock.\n",
             lock->ToString().c_str(),
             tid().raw());
      ReportStackTrace();
    }

    NewSegmentForLockingEvent();

    if (G_flags->verbosity >= 2)
      Printf("T%d %sUnlock %p; %s\n", 
           tid_.raw(), 
           is_w_lock ? "Wr" : "Rd", 
           lock_addr, 
           LockSet::ToString(lsid(is_w_lock)).c_str());
  }

  LSID lsid(bool is_w) {
    return is_w ? wr_lockset_ : rd_lockset_;
  }

  // CondVar
  
  void HandleWaitBefore(uintptr_t cv, uintptr_t mu) {
    CHECK(wait_cv_and_mu_set_ == false);
    wait_cv_and_mu_set_ = true;
    wait_cv_ = cv;
    wait_mu_ = mu;
    if (mu) {
      HandleUnlock(mu);
    }
  }
  void HandleWaitAfter(bool timed_out) {
    CHECK(wait_cv_and_mu_set_ == true);
    wait_cv_and_mu_set_ = false;
    HandleWait(wait_cv_, wait_mu_, timed_out);
    wait_mu_ = 0;
    wait_cv_ = 0;
  }

  void HandleWait(uintptr_t cv, uintptr_t mu, bool timed_out) {
    if (mu) {
      // HandleUnlock is called in HandleWaitBefore().
      HandleLock(mu, true);
    }

    SignallerMap::iterator it = signaller_map_->find(cv);
    if (it != signaller_map_->end()) {
      const VTS *signaller_vts = it->second.vts;
      NewSegmentForWait(signaller_vts);
    }

    if (G_flags->verbosity >= 2) {
      Printf("T%d: %s: %p (%p):\n    %s %s\n", tid_.raw(), 
             timed_out ? "TimedOutWait" : "Wait",
             cv, mu,
             vts()->ToString().c_str(),
             Segment::ToString(sid()).c_str()
             );
    }
  }


  void HandleSignal(uintptr_t cv) {
    Signaller *signaller = &(*signaller_map_)[cv];
    if (!signaller->vts) {
      signaller->vts = vts()->Clone();
    } else {
      VTS *new_vts = VTS::JoinAndTick(signaller->vts, vts(), TID());
      VTS::Delete(signaller->vts);
      signaller->vts = new_vts;
    }
    NewSegmentForSignal();
    if (G_flags->verbosity >= 2)
      Printf("T%d: Signal: %p:\n    %s %s\n    %s\n", tid_.raw(), cv, 
             vts()->ToString().c_str(), Segment::ToString(sid()).c_str(),
             (signaller->vts)->ToString().c_str());
      
  }


  void INLINE NewSegmentWithoutUnrefingOld(const char *call_site, VTS *new_vts) {
    DCHECK(new_vts);
    SID new_sid = Segment::AddNewSegment(tid(), new_vts, 
                                         rd_lockset_, wr_lockset_);
    SID old_sid = sid();
    sid_ = new_sid;
    Segment::Ref(new_sid, "Thread::NewSegmentWithoutUnrefingOld");

    FillEmbeddedStackTrace(segment()->embedded_stack_trace());
    if (0) 
    Printf("2: %s T%d/S%d old_sid=%d NewSegment: %s\n", call_site, 
           tid().raw(), sid().raw(), old_sid.raw(),
         vts()->ToString().c_str());
  }

  void INLINE NewSegment(const char *call_site, VTS *new_vts) {
    SID old_sid = sid();
    NewSegmentWithoutUnrefingOld(call_site, new_vts);
    Segment::Unref(old_sid, "Thread::NewSegment");
  }

  void NewSegmentForLockingEvent() {
    NewSegment("NewSegmentForLockingEvent", vts()->Clone());
  }


  void SetTopPc(uintptr_t pc) {
    DCHECK(!call_stack_.empty());
    call_stack_.back() = pc;
  }

  void INLINE HandleSblockEnter(uintptr_t pc) {
    if (!G_flags->keep_history) return;
    if (g_so_far_only_one_thread) return;

    SetTopPc(pc);

    Segment *seg = segment();
    uintptr_t *emb_trace = seg->embedded_stack_trace();

    size_t n = call_stack_.size();
    // check at most 3 top entries of the call stack. 
    // If they match the current segment, don't create a new segment.
    if (*emb_trace &&  // This stack trace was filled
        call_stack_.size() >= 3 &&
        emb_trace[0] == call_stack_[n-1] && 
        emb_trace[1] == call_stack_[n-2] && 
        emb_trace[2] == call_stack_[n-3]) {  
      G_stats->history_uses_same_segment++;
      return;
    }


    if (seg->ref_count() == 1) {
      // The current segment is not used anywhere, 
      // so just replace the stack trace in it.
      FillEmbeddedStackTrace(emb_trace);
      G_stats->history_reuses_segment++;
      return;
    }
    
    G_stats->history_creates_new_segment++;
    VTS *new_vts = vts()->Clone();
    NewSegment("HandleSblockEnter", new_vts);
  }

  void NewSegmentForWait(const VTS *signaller_vts) {
    const VTS *current_vts   = vts();
    if (0)
    Printf("T%d NewSegmentForWait: \n  %s\n  %s\n", tid().raw(), 
           current_vts->ToString().c_str(),
           signaller_vts->ToString().c_str()
           );
    // We don't want to create a happens-before arc if it will be redundant.
    if (!VTS::HappensBeforeCached(signaller_vts, current_vts)) {
      VTS *new_vts = VTS::JoinAndTick(current_vts, signaller_vts, TID());
      NewSegment("NewSegmentForWait", new_vts);
    }
    DCHECK(VTS::HappensBeforeCached(signaller_vts, vts()));
  }


  void NewSegmentForSignal() {
    VTS *cur_vts = vts();
    VTS *new_vts = VTS::CopyAndTick(cur_vts, tid());
    NewSegment("NewSegmentForSignal", new_vts);
  }


  // Barrier 
  void HandleBarrierBefore(uintptr_t barrier) {
    barrier_addr_ = barrier;
    HandleSignal(barrier_addr_);
  }

  void HandleBarrierAfter() {
    HandleWait(barrier_addr_, 0, false);
  }

  // Call stack  -------------
  
  void PushCallStack(uintptr_t pc) {
    call_stack_.push_back(pc);
  }

  void PopCallStack() {
    CHECK(!call_stack_.empty());
    call_stack_.pop_back();
  }

  void HandleRtnCall(uintptr_t call_pc, uintptr_t target_pc) {
    if (!call_stack_.empty()) {
      call_stack_.back() = call_pc;
    }
    PushCallStack(target_pc);
  }

  void HandleRtnExit() {
    if (!call_stack_.empty()) {
      call_stack_.pop_back();
    }
  }



  uintptr_t GetCallstackEntry(size_t offset_from_top) {
    if (offset_from_top >= call_stack_.size()) return 0;
    return call_stack_[call_stack_.size() - offset_from_top - 1];
  }


  string CallStackRtnName(size_t offset_from_top = 0) const {
    if (call_stack_.size() <= offset_from_top)
      return "";
    uintptr_t pc = call_stack_[call_stack_.size() - offset_from_top - 1];
    return PcToRtnNameWithStats(pc, false);
  }



  string CallStackToStringRtnOnly(int len) {
    string res;
    for (int i = 0; i < len; i++) {
      if (i)
        res += " ";
      res += CallStackRtnName(i);
    }
    return res;
  }

  uintptr_t CallStackTopPc() const {
    if (call_stack_.empty()) 
      return 0;
    return call_stack_.back();
  }

  INLINE void FillEmbeddedStackTrace(uintptr_t *emb_trace) {
    size_t size = min(call_stack_.size(), (size_t)kSizeOfHistoryStackTrace);
    size_t idx = call_stack_.size() - 1;
    for (size_t i = 0; i < size; i++, idx--) {
      emb_trace[i] = call_stack_[idx];
    }
    if (size < (size_t) kSizeOfHistoryStackTrace) {
      emb_trace[size] = 0;
    }
  }

  INLINE void FillStackTrace(StackTrace *trace, size_t size) {
    size_t idx = call_stack_.size() - 1;
    for (size_t i = 0; i < size; i++, idx--) {
      trace->Set(i, call_stack_[idx]);
    }
  }

  INLINE StackTrace *CreateStackTrace(uintptr_t pc = 0, 
                                      int max_len = -1, 
                                      int capacity = 0) {
    if (!call_stack_.empty() && pc) {
      call_stack_.back() = pc;
    }
    if (max_len <= 0) {
      max_len = G_flags->num_callers;
    }
    int size = call_stack_.size();
    if (size > max_len) 
      size = max_len;
    StackTrace *res = StackTrace::CreateNewEmptyStackTrace(size, capacity);
    FillStackTrace(res, size);
    return res;
  }


  void ReportStackTrace(uintptr_t pc = 0, int max_len = -1) {
    StackTrace *trace = CreateStackTrace(pc, max_len);
    Report("%s", trace->ToString().c_str());
    StackTrace::Delete(trace);
  }


  bool NOINLINE CallStackContainsDtor() {
    // TODO: can check this w/o demangling?
    for (int i = call_stack_.size() - 1; i >= 0; i--) {
      uintptr_t pc = call_stack_[i];
      string str = PcToRtnNameWithStats(pc, false);
      // shortcut: if 'D' is not present in mangled name -- not a DTOR
      if (str.find('D') == string::npos) continue;

      string demangled = PcToRtnNameWithStats(pc, true);
      if (demangled.find('~') != string::npos) {
        return true;
      }
    }
    return false;
  }


  static void ForgetAllState() {
    G_flags->debug_level = 2;
    for (int i = 0; i < Thread::NumberOfThreads(); i++) {
      Thread *thr = Get(TID(i));
      VTS *singleton_vts = VTS::CreateSingleton(TID(i), 2);
      thr->NewSegmentWithoutUnrefingOld("ForgetAllState", singleton_vts);
      if (thr->vts_at_exit_) {
        VTS::Delete(thr->vts_at_exit_);
        thr->vts_at_exit_ = singleton_vts->Clone();
      }
    }
    signaller_map_->ClearAndDeleteElements();
  }

  static void InitClassMembers() {
    all_threads_        = new vector<Thread*>;
    signaller_map_      = new SignallerMap;
    ptid_to_tid_        = new map<pthread_t, TID>;
  }



 private:

  bool is_running_;
  string thread_name_;



  TID    tid_;          ///< This thread's tid.
  SID    sid_;          ///< Current segment ID.
  TID    parent_tid_;   ///< Parent's tid.
  uintptr_t  max_sp_;    
  uintptr_t  min_sp_;    
  uintptr_t  cur_sp_;   ///< Current sp value.
  uintptr_t  new_sp_;   ///< Value of SP after recent change.
  StackTrace *creation_context_;
  bool      announced_;


  pthread_t join_child_ptid_;


  LSID   rd_lockset_;
  LSID   wr_lockset_;

  // The following lock_mu_ and wait_* fields are to simplify
  // Handle*{Before,After} calls.
  uintptr_t lock_mu_;

  uintptr_t wait_cv_;
  uintptr_t wait_mu_;
  bool      wait_cv_and_mu_set_;

  bool      bus_lock_is_set_;

  uintptr_t barrier_addr_;

  int      ignore_[2]; // 0 for reads, 1 for writes.
  StackTrace *ignore_context_[2];


  VTS *vts_at_exit_;



  vector<uintptr_t> call_stack_;

  // All threads. The main thread has tid 0.
  static vector<Thread*> *all_threads_;

  struct Signaller {
    VTS *vts;
  };
  
  class SignallerMap: public map<uintptr_t, Signaller> {
    public: 
     void ClearAndDeleteElements() {
       for (iterator it = begin(); it != end(); ++it) {
         VTS::Delete(it->second.vts);
       }
       clear();
     }
  };
  // signaller address -> VTS
  static SignallerMap *signaller_map_;


  static map<pthread_t, TID> *ptid_to_tid_;
};

// Thread:: static members
vector<Thread*>            *Thread::all_threads_;
Thread::SignallerMap       *Thread::signaller_map_;
map<pthread_t, TID>        *Thread::ptid_to_tid_;


//--------- PCQ --------------------- {{{1
struct PCQ {
  uintptr_t pcq_addr;
  deque<VTS*> putters;
};

typedef map<uintptr_t, PCQ> PCQMap;
static PCQMap *g_pcq_map;

//--------- Forget all state -------- {{{1
// We need to forget all state and start over because we've 
// run out of some resources (most likely, segment IDs). 
// Experimental code, not stable.
static void ForgetAllStateAndStartOver(const char *reason) {
  if (0) {
    Report("INFO: %s.\n", reason);
    Report("INFO: Thread Sanitizer will now forget all history.\n");
    Report("INFO: This is experimental, and may fail!\n");
    if (G_flags->keep_history > 0) {
      Report("INFO: Consider re-running with --keep_history=0\n");
    }
    if (G_flags->show_stats) {
        G_stats->PrintStats();
    }
  }

  G_stats->n_forgets++;

  Segment::ForgetAllState();
  SegmentSet::ForgetAllState();
  G_cache->ForgetAllState();
  Thread::ForgetAllState();

  g_publish_info_map->clear();

  for (PCQMap::iterator it = g_pcq_map->begin(); it != g_pcq_map->end(); ++it) {
    PCQ &pcq = it->second;
    for (deque<VTS*>::iterator it2 = pcq.putters.begin(); 
         it2 != pcq.putters.end(); ++it2) {
      VTS::Delete(*it2);
      *it2 = VTS::CreateSingleton(TID(0), 1);
    }
  }
}
//--------- Expected Race ---------------------- {{{1
// Information about expected races.
struct ExpectedRace {
  bool        is_benign;
  int         count;
  const char *description;
  uintptr_t   pc;
};

typedef  map<uintptr_t, ExpectedRace> ExpectedRacesMap;
static ExpectedRacesMap *G_expected_races_map;


//--------- Heap info ---------------------- {{{1
// Information about heap memory.
struct HeapInfo {
  uintptr_t   ptr;
  uintptr_t   size;
  TID         tid;
  StackTrace *stack_trace;
};


typedef map<uintptr_t, HeapInfo> HeapMap;
static HeapMap *G_heap_map;


static HeapInfo IsHeapMem(uintptr_t a) {
  HeapInfo null_info;
  CHECK(G_heap_map);
  null_info.ptr = 0;
  null_info.size = 0;
  null_info.stack_trace = 0;
  HeapMap::iterator it = G_heap_map->lower_bound(a);
  if (it == G_heap_map->end()) 
    return null_info;
  CHECK(it->second.ptr >= a);
  if (it->second.ptr == a) 
    return it->second;
  if (it != G_heap_map->begin()) {
    // try prev
    --it;
    HeapInfo &info = it->second;
    CHECK(info.ptr < a);
    if (info.ptr + info.size > a) 
      return info;
  }
  return null_info;
}





//--------- ThreadSanitizerReport -------------- {{{1
struct ThreadSanitizerReport {
  // Types of reports. Right now we support races only.
  enum ReportType {
    DATA_RACE
  };

  ReportType type;
};

struct ThreadSanitizerDataRaceReport : public ThreadSanitizerReport {
  uintptr_t   racey_addr;
  string      racey_addr_description;
  uintptr_t   last_access_size;
  TID         last_access_tid;
  SID         last_access_sid;
  StackTrace *last_access_stack_trace;
  bool        last_access_is_w;
  LSID        last_acces_lsid[2]; 

  ShadowValue new_sval;
  ShadowValue old_sval;

  bool        is_expected;
  bool        racey_addr_was_published;
};


//--------- Report Storage --------------------- {{{1
class ReportStorage {
 public:
  bool NOINLINE AddReport(TID tid, uintptr_t pc, bool is_w, uintptr_t addr, 
                          int size, 
                          ShadowValue old_sval, ShadowValue new_sval, 
                          bool is_published) {
    Thread *thr = Thread::Get(tid);
//    if (old_sval.racey()) {
//      return false;
//    }


    bool in_dtor = G_flags->ignore_in_dtor && thr->CallStackContainsDtor();

    bool is_expected = false;
    if (G_expected_races_map->count(addr)) {
      is_expected = true;
      (*G_expected_races_map)[addr].count++;
    }

    if (!is_expected && in_dtor) return false;

    if (is_expected && !G_flags->show_expected_races) return false;

    StackTrace *stack_trace = thr->CreateStackTrace(pc);
    int n_reports_for_this_context = reported_stacks_[stack_trace]++;

    if (n_reports_for_this_context > 0) {
      // we already reported a race here. 
      StackTrace::Delete(stack_trace);
      return false;
    }


    ThreadSanitizerDataRaceReport *race_report = 
        new ThreadSanitizerDataRaceReport;

    race_report->type = ThreadSanitizerReport::DATA_RACE;
    race_report->new_sval = new_sval;
    race_report->old_sval = old_sval;
    race_report->is_expected = is_expected;
    race_report->last_access_is_w = is_w;
    race_report->racey_addr = addr;
    race_report->racey_addr_description = DescribeMemory(addr);
    race_report->last_access_tid = tid;
    race_report->last_access_sid = thr->sid();
    race_report->last_access_size = size;
    race_report->last_access_stack_trace = stack_trace;
    race_report->racey_addr_was_published = is_published;
    race_report->last_acces_lsid[false] = thr->lsid(false);
    race_report->last_acces_lsid[true] = thr->lsid(true);

    Segment *seg = Segment::Get(thr->sid());
    CHECK(thr->lsid(false) == seg->lsid(false));
    CHECK(thr->lsid(true) == seg->lsid(true));


    ExeContext *valgrindish_context = stack_trace->ToValgrindExeContext();
    if (VG_(unique_error)(GetVgTid(), XS_Race, 0, NULL, 
                          race_report, valgrindish_context, 
                          False, False, False)) {
      // This is a HACK. Valgrind ExeContext is sometimes broken and hence 
      // suppressions may fail. We create an ExeContext from tsan's stack trace 
      // and check if it should be suppressed (see comments for unique_error).
      if (G_flags->show_valgrind_context) {
        Report("------ ThreadSanitizer StackTrace: -------\n");
        VG_(pp_ExeContext)(valgrindish_context);
        Report("------ valgrind ExeContext: -------\n");
        VG_(pp_ExeContext)(VG_(record_ExeContext)(VG_(get_running_tid)(), 0));
      }
      // this error was suppressed -- return.
      delete race_report;
      return false;
    }

    if (ERROR_IS_RECORDED != VG_(maybe_record_error)(
        GetVgTid(), XS_Race, 0, NULL, race_report) ) {
      StackTrace::Delete(stack_trace);
      delete race_report;
      return false;
    }

    SegmentSet::RefIfNotEmpty(old_sval.rd_ssid(), "AddReport");
    SegmentSet::RefIfNotEmpty(new_sval.rd_ssid(), "AddReport");
    SegmentSet::RefIfNotEmpty(old_sval.wr_ssid(), "AddReport");
    SegmentSet::RefIfNotEmpty(new_sval.wr_ssid(), "AddReport");

    return true;
  }

  static void AnnounceThreadsInSegmentSet(SSID ssid) {
    if (ssid.IsEmpty()) return;
    for (int s = 0; s < SegmentSet::Size(ssid); s++) {
      Segment *seg = SegmentSet::GetSegmentForNonSingleton(ssid, s, __LINE__);
      Thread::Get(seg->tid())->Announce();
    }
  }



  static void PrintConcurrentSegmentSet(SSID ssid, TID tid, SID sid, 
                                        LSID lsid, bool is_w, 
                                        const char *descr, set<LID> *locks) {
    if (ssid.IsEmpty()) return;
    bool printed_header = false;
    for (int s = 0; s < SegmentSet::Size(ssid); s++) {
      SID concurrent_sid = SegmentSet::GetSID(ssid, s, __LINE__);
      Segment *seg = Segment::Get(concurrent_sid);
      if (Segment::HappensBeforeOrSameThread(concurrent_sid, sid)) continue;
      if (!LockSet::IntersectionIsEmpty(lsid, seg->lsid(is_w))) continue; 
      Thread *concurrent_thr = Thread::Get(seg->tid());
      if (!printed_header) {
        Report("  %sConcurrent %s happened at (OR AFTER) these points:%s\n",
               c_magenta, descr, c_default);
        printed_header = true;
      }

      Report("   %s (%s):\n", 
             concurrent_thr->ThreadName().c_str(),
             TwoLockSetsToString(seg->lsid(false), 
                                 seg->lsid(true)).c_str());
      if (G_flags->show_states) {
        Report("   S%d\n", concurrent_sid.raw());
      }
      LockSet::AddLocksToSet(seg->lsid(false), locks);
      LockSet::AddLocksToSet(seg->lsid(true), locks);
      Report("%s", StackTrace::EmbeddedStackTraceToString(
          seg->embedded_stack_trace(), kSizeOfHistoryStackTrace).c_str());
    }
  }


  static void PrintReport(ThreadSanitizerReport *report) { 
    // right now -- only data races.
    CHECK(report);
    CHECK(report->type == ThreadSanitizerReport::DATA_RACE);
    ThreadSanitizerDataRaceReport *race =
        (ThreadSanitizerDataRaceReport*)report;

    AnnounceThreadsInSegmentSet(race->new_sval.rd_ssid());
    AnnounceThreadsInSegmentSet(race->new_sval.wr_ssid());
    bool is_w = race->last_access_is_w;
    TID     tid = race->last_access_tid;
    Thread *thr = Thread::Get(tid);
    SID     sid = race->last_access_sid;
    LSID    lsid = race->last_acces_lsid[is_w];



    set<LID> all_locks;

    if (G_flags->show_valgrind_context) {
      ExeContext *context = VG_(record_ExeContext)(VG_(get_running_tid)(), 0);
      VG_(pp_ExeContext)(context);
    }

    n_reports++;
    if (G_flags->html) {
      Report("<b id=race%d>Race report #%d; </b>"
             "<a href=\"#race%d\">Next;</a>  " 
             "<a href=\"#race%d\">Prev;</a>\n", 
             n_reports, n_reports, n_reports+1, n_reports-1);
    }

    if (!G_flags->summary_file.empty()) {
      char buff[100];
      sprintf(buff, "ThreadSanitizer: %d data race(s) reported\n", n_reports);
      // We overwrite the contents of this file with the new summary. 
      // We don't do that at the end because even if we crash later
      // we will already have the summary.
      OpenFileWriteStringAndClose(G_flags->summary_file, buff);
    }

    // Note the {{{ and }}}. These are for vim folds.
    Report("%sWARNING: %s data race during %s of size %d at %p: {{{%s\n", 
           c_red,
           race->is_expected ? "Expected" : "Possible", 
           is_w ? "write" : "read", 
           race->last_access_size, 
           race->racey_addr,
           c_default);
    CHECK(race->last_access_stack_trace);
    LockSet::AddLocksToSet(race->last_acces_lsid[false], &all_locks);
    LockSet::AddLocksToSet(race->last_acces_lsid[true], &all_locks);
    Report("   %s (%s):\n", 
           thr->ThreadName().c_str(),
           TwoLockSetsToString(race->last_acces_lsid[false], 
                               race->last_acces_lsid[true]).c_str()
          );

    Report("%s", race->last_access_stack_trace->ToString().c_str());
    //Report(" sid=%d; vts=%s\n", thr->sid().raw(), 
    //       thr->vts()->ToString().c_str());
    if (G_flags->show_states) {
      Report(" old state: %s\n", race->old_sval.ToString().c_str());
      Report(" new state: %s\n", race->new_sval.ToString().c_str());
    }
    if (G_flags->keep_history) {
      PrintConcurrentSegmentSet(race->new_sval.wr_ssid(), 
                                tid, sid, lsid, true, "write(s)", &all_locks);
      if (is_w) {
        PrintConcurrentSegmentSet(race->new_sval.rd_ssid(), 
                                  tid, sid, lsid, false, "read(s)", &all_locks);
      }
    } else {
      Report("  %sAccess history is disabled. " 
             "Consider running with --keep-history=1 for better reports.%s\n",
             c_cyan, c_default);
    }

    if (race->racey_addr_was_published) {
      Report(" This memory was published\n");
    }
    if (race->racey_addr_description.size() > 0) {
      Report("%s", race->racey_addr_description.c_str());
    }
    if (race->is_expected) {
      Report(" Description: \"%s\"\n", 
             (*G_expected_races_map)[race->racey_addr].description);
    }
    set<LID>  locks_reported;

    if (!all_locks.empty()) {
      string all_locks_str;
      for (set<LID>::iterator it = all_locks.begin(); 
           it != all_locks.end(); ++it) {
        LID lid = *it;
        char buff[100];
        sprintf(buff, "L%d", lid.raw());
        if (it != all_locks.begin())
          all_locks_str += ", ";
        all_locks_str += buff;
      }
      Report("  %sLocks involved in this report "
             "(reporting last lock sites):%s {%s}\n",
             c_green, c_default, all_locks_str.c_str());

      for (set<LID>::iterator it = all_locks.begin(); 
           it != all_locks.end(); ++it) {
        LID lid = *it;
        Lock::ReportLockWithOrWithoutContext(lid, true);
      }
    }

    Report("}}}\n");
  }

 private:

  static string DescribeMemory(uintptr_t a) {
    const int kBufLen = 1023;
    char buff[kBufLen+1];
    HeapInfo heap_info = IsHeapMem(a);
    if (heap_info.ptr) {
      sprintf(buff, "  %sLocation %p is %ld bytes inside a block starting at %p"
             " of size %ld allocated by T%d from heap:%s\n", 
             c_blue, 
             (void*)a, (long)(a - heap_info.ptr), (void*)heap_info.ptr, 
             (long)heap_info.size, heap_info.tid.raw(), c_default);
      return string(buff) + heap_info.stack_trace->ToString().c_str();
    } 


    OffT offset;
    if (VG_(get_datasym_and_offset)(a, (Char*)buff, kBufLen, &offset) ){
      string symbol_descr = buff;
      sprintf(buff, "  %sAddress %p is %d bytes inside data symbol \"", 
              c_blue, (void*)a, (int)offset);
      return buff + symbol_descr + "\"" + c_default + "\n";
    }
    return "";
  }

  

  map<StackTrace *, int, StackTrace::Less> reported_stacks_;
  static int n_reports;
};

// static 
int ReportStorage::n_reports = 0; 

//--------- Event Sampling ---------------- {{{1
// This class samples (profiles) events.
// Instances of this class should all be static.
class EventSampler {
 public:

  // Sample one event 
  void Sample(TID tid, const char *event_name) {
    CHECK(G_flags->sample_events != 0);
    (counter_)++;
    if ((counter_ & ((1 << G_flags->sample_events) - 1)) != 0) 
      return;
    string pos =  Thread::Get(tid)->
        CallStackToStringRtnOnly(G_flags->sample_events_depth);
    (*samples_)[event_name][pos]++;
  }
   
  // Show existing samples
  static void ShowSamples() {
    if (G_flags->sample_events == 0) return;
    for (SampleMapMap::iterator it1 = samples_->begin(); 
         it1 != samples_->end(); ++it1) {
      string name = it1->first;
      SampleMap &m = it1->second;
      int total = 0;
      for (SampleMap::iterator it2 = m.begin(); it2 != m.end(); it2++) {
        total += it2->second;
      }

      map<int, string> reverted_map;
      for (SampleMap::iterator it2 = m.begin(); it2 != m.end(); it2++) {
        int n_samples = it2->second;
        if (n_samples * 1000 < total) continue;
        reverted_map[n_samples] = it2->first;
      }
      Printf("%s: total samples %'d (~%'lld events)\n", name.c_str(), 
             total,
             (int64_t)total << G_flags->sample_events);
      for (map<int,string>::iterator it = reverted_map.begin(); 
           it != reverted_map.end(); ++it) {
        Printf("%s: %d%%%% %s\n", name.c_str(), 
               (it->first * 1000) / total, it->second.c_str());
      }
      Printf("\n");
    }
  }


  static void InitClassMembers() {
    samples_ = new SampleMapMap;
  }
 private: 

  int counter_;

  typedef map<string, int> SampleMap;
  typedef map<string, SampleMap> SampleMapMap;
  static SampleMapMap *samples_;     
};

EventSampler::SampleMapMap *EventSampler::samples_;



//--------- Detector ---------------------- {{{1
// Basically, a collection of event handlers.
class Detector {
 public:
  INLINE void HandleOneEvent(Event *event) {
    cur_tid_ = TID(event->tid());
    cur_thread_ = Thread::GetIfExists(cur_tid_);
    HandleOneEventInternal(event);
  }

  void INLINE HandleMemoryAccess(int32_t tid, 
                          uintptr_t addr, uintptr_t size, 
                          bool is_w) {
    G_stats->events[is_w ? WRITE : READ]++;
    HandleMemoryAccessInternal(TID(tid), addr, size, is_w);
  }

  void INLINE HandleStackMemChange(int32_t tid, uintptr_t addr,   
                                   uintptr_t size, bool is_new) {
    G_stats->events[is_new ? STACK_MEM_NEW : STACK_MEM_DIE]++;
    HandleStackMem(TID(tid), addr, size, is_new);
  }

  void HandleProgramEnd() {
    // Report("ThreadSanitizerValgrind: done\n");
    // check if we found all expected races (for unit tests only).
    for (ExpectedRacesMap::iterator it = G_expected_races_map->begin(); 
         it != G_expected_races_map->end(); ++it) {
      ExpectedRace race = it->second;
      if (race.count == 0 && !race.is_benign) {
        Printf("Missing an expected race on %p: %s (annotated at %s)\n", 
               it->first, 
               race.description,
               PcToRtnNameAndFilePos(race.pc).c_str());
      }
    }

    // check if there is not deleted memory 
    // (for debugging free() interceptors, not for leak detection)
    if (0) {
      for (HeapMap::iterator it = G_heap_map->begin(); 
           it != G_heap_map->end(); ++it) {
        HeapInfo &info = it->second;
        Printf("Not free()-ed memory: \n%s\n", 
               info.stack_trace->ToString().c_str());
      }
    }

    // CheckLiveSegments();
    
    EventSampler::ShowSamples();
    ShowStats();
    ShowProcSelfStatus();
    // Report("ThreadSanitizerValgrind: exiting\n");
  }



  void INLINE HandleSblockEnter(TID tid, uintptr_t pc) {
    Thread::Get(tid)->HandleSblockEnter(pc);  
    G_stats->events[SBLOCK_ENTER]++;

    // Are we out of segment IDs?
    if (Segment::NumberOfSegments() > ((kMaxSID * 15) / 16)) {
      ForgetAllStateAndStartOver("Thread Sanitizer has run out of segment IDs");
    }

    if(UNLIKELY(G_flags->sample_events)) {
      static EventSampler sampler;
      sampler.Sample(tid, "SampleSblockEnter");
    }
  }

  void HandleRtnCall(TID tid, uintptr_t call_pc, uintptr_t target_pc) {
    G_stats->events[RTN_CALL]++;
    Thread::Get(tid)->HandleRtnCall(call_pc, target_pc);
  }

  void HandleRtnExit(TID tid) {
    G_stats->events[RTN_EXIT]++;
    Thread::Get(tid)->HandleRtnExit();
  }


 private:

  void CheckLiveSegments() {
    if (!(DEBUG_MODE && G_flags->debug_level >= 3)) 
      return;
#if 0
    Cache::Map::iterator it;
    int n_live_lines = 0;
    set<SID> sids;
    set<SSID> ssids;
    for (it = G_cache->map_.begin(); it != G_cache->map_.end(); ++it) {
      CacheLine *line = it->second;
      CHECK(line->tag() == it->first);
      if (line->used().Empty()) continue;
      n_live_lines++;
      for (uintptr_t i = 0; i < 64; i++) {
        if (!line->used().Get(i)) continue;
        ShadowValue sval = line->GetValue(i);
        SSID ssid = sval.msm1_ssid();
        if (ssid.IsSingleton()) {
          sids.insert(ssid.Singleton());
        } else {
          ssids.insert(ssid);
          SegmentSet *sset = SegmentSet::Get(ssid);
          for (int j = 0; j < sset->size(); j++) {
            SID sid = sset->GetSID(j);
            sids.insert(sid);
          }
        }
      }
    }
    Printf("lines total  : %d\n", (int)G_cache->map_.size());
    Printf("lines live   : %d\n", n_live_lines);
    Printf("Segs  live   : %d  /  %d\n", (int)sids.size(), 
           Segment::NumLiveSegments());
    Printf("SegSets live : %d\n", (int)ssids.size());
    for (int s = 1; s < Segment::NumberOfSegments(); s++) {
      SID sid(s);
      if (Segment::Alive(sid) && sids.count(sid) == 0) {
        Printf("zz: %d\n", sid.raw());
      }
    }
#endif
  }

  void ShowProcSelfStatus() {
    if (G_flags->show_proc_self_status) {
      string str = ReadFileToString("/proc/self/status");
      Printf("%s", str.c_str());
    }
  }

  void ShowStats() {
    if (G_flags->show_stats) {
      G_stats->PrintStats();
      G_cache->PrintStorageStats();
    }
  }

  void INLINE HandleOneEventInternal(Event *event) {
    ScopedMallocCostCenter malloc_cc("HandleOneEventInternal");

    e_ = event;
    DCHECK(event);
    EventType type = e_->type();
    DCHECK(type != NOOP);
    G_stats->events[type]++;
    if (type != THR_START) {
      Thread::Get(TID(e_->tid()))->SetTopPc(e_->pc());
    }

    switch(type) {
      case THR_CREATE_AFTER   : HandleThreadCreateAfter(); break;
      case THR_START   : 
        HandleThreadStart(TID(e_->tid()), TID(e_->info()), e_->pc());
        break;

      case THR_JOIN_BEFORE    : HandleThreadJoinBefore();   break;
      case THR_JOIN_AFTER     : HandleThreadJoinAfter();   break;

      case THR_END     : HandleThreadEnd(TID(e_->tid()));     break;
      case MALLOC      : HandleMalloc();     break;
      case FREE        : HandleFree();         break;                          


      case LOCK_BEFORE : HandleLockBefore();   break;
      case WRITER_LOCK : HandleLock(true);     break;
      case READER_LOCK : HandleLock(false);    break;
      case UNLOCK      : HandleUnlock();       break;

      case LOCK_CREATE: 
      case LOCK_DESTROY: HandleLockCreateOrDestroy(); break;

      case BUS_LOCK_ACQUIRE   : HandleBusLock(true);  break;
      case BUS_LOCK_RELEASE   : HandleBusLock(false); break;

      case SIGNAL      : HandleSignal();       break;
      case WAIT_BEFORE : HandleWaitBefore();   break;
      case WAIT_AFTER  : HandleWaitAfter(false);    break;
      case TWAIT_AFTER : HandleWaitAfter(true);   break;

      case BARRIER_BEFORE : HandleBarrierBefore(); break;
      case BARRIER_AFTER  : HandleBarrierAfter();  break;

      case PCQ_CREATE   : HandlePcqCreate();   break;
      case PCQ_DESTROY  : HandlePcqDestroy();  break;
      case PCQ_PUT      : HandlePcqPut();      break;
      case PCQ_GET      : HandlePcqGet();      break;

  
      case EXPECT_RACE : HandleExpectRace();   break;

      case HB_LOCK     : HandleHBLock();       break;

      case IGNORE_READS_BEG: HandleIgnore(false, true); break;
      case IGNORE_READS_END: HandleIgnore(false, false); break;
      case IGNORE_WRITES_BEG: HandleIgnore(true, true); break;
      case IGNORE_WRITES_END: HandleIgnore(true, false); break;

      case SET_THREAD_NAME: 
        cur_thread_->set_thread_name((const char*)e_->a()); 
        break;

      case PUBLISH_RANGE : HandlePublishRange();   break;

      case TRACE_MEM   : HandleTraceMem();        break;
      case VERBOSITY   : e_->Print(); G_flags->verbosity = e_->info(); break;
      case STACK_TRACE : HandleStackTrace();   break;
      case NOOP        : CHECK(0);            break; // can't happen.
      default                 : break;
    }

    if (DEBUG_MODE && G_flags->verbosity  >= 5) {
      Printf("<< ");
      e_->Print();
    }
  }

  // PCQ_CREATE, PCQ_DESTROY, PCQ_PUT, PCQ_GET 
  void HandlePcqCreate() {
    if (G_flags->verbosity >= 2) {
      e_->Print();
    }
    PCQ pcq;
    pcq.pcq_addr = e_->a();
    CHECK(!g_pcq_map->count(e_->a()));
    (*g_pcq_map)[e_->a()] = pcq;
  }
  void HandlePcqDestroy() {
    if (G_flags->verbosity >= 2) {
      e_->Print();
    }
    CHECK(g_pcq_map->count(e_->a()));
    g_pcq_map->erase(e_->a());
  }
  void HandlePcqPut() {
    if (G_flags->verbosity >= 2) {
      e_->Print();
    }
    PCQ &pcq = (*g_pcq_map)[e_->a()];
    CHECK(pcq.pcq_addr == e_->a());
    VTS *vts = cur_thread_->segment()->vts()->Clone();
    pcq.putters.push_back(vts);
    cur_thread_->NewSegmentForSignal();
  }
  void HandlePcqGet() {
    if (G_flags->verbosity >= 2) {
      e_->Print();
    }
    PCQ &pcq = (*g_pcq_map)[e_->a()];
    CHECK(pcq.pcq_addr == e_->a());
    CHECK(!pcq.putters.empty());
    VTS *putter = pcq.putters.front();
    pcq.putters.pop_front();
    CHECK(putter);
    cur_thread_->NewSegmentForWait(putter);
    VTS::Delete(putter);
  }

  // PUBLISH_RANGE
  void HandlePublishRange() {
    if (G_flags->verbosity >= 2) {
      e_->Print();
    }
    uintptr_t mem = e_->a();
    uintptr_t size = e_->info();

    PublishRange(mem, mem + size, cur_thread_->segment()->vts()->Clone());

    cur_thread_->NewSegmentForSignal();
    //Printf("Publish: [%p, %p)\n", mem, mem+size);
  }

  void HandleIgnore(bool is_w, bool on) {
    if (G_flags->verbosity >= 2) {
      e_->Print();
    }
    cur_thread_->set_ignore(is_w, on);
  }

  // EXPECT_RACE
  void HandleExpectRace() {
    ExpectedRace expected_race;
    expected_race.count = 0;
    expected_race.is_benign = (bool)e_->info();
    expected_race.description = (const char*)e_->pc();
    expected_race.pc = cur_thread_->GetCallstackEntry(1);
    (*G_expected_races_map)[e_->a()] = expected_race;
    if (G_flags->verbosity >= 2) {
      Printf("T%d: EXPECT_RACE: %p '%s'\n", e_->tid(), 
             e_->a(), 
             expected_race.description);
      cur_thread_->ReportStackTrace(e_->pc());
    }

  }

  void HandleStackTrace() {
    e_->Print();
    cur_thread_->ReportStackTrace();
  }

  // HB_LOCK
  void HandleHBLock() {
    if (G_flags->verbosity >= 2) {
      e_->Print();
//      cur_thread_->ReportStackTrace();
    }
//    cur_thread_->ReportStackTrace(); 
    Lock *lock = Lock::LookupOrCreate(e_->a());
    CHECK(lock);
    lock->set_is_pure_happens_before(true);
  }


  void INLINE ClearMemoryStateOnStackNewMem(uintptr_t a, uintptr_t b) {
    ClearMemoryState(a, b, true);
  }
  void INLINE ClearMemoryStateOnStackDieMem(uintptr_t a, uintptr_t b) {
    ClearMemoryState(a, b, false);
  }

  void INLINE HandleStackMem(TID tid, uintptr_t addr, 
                             uintptr_t size, bool is_new_mem) {
    uintptr_t a = addr;
    DCHECK(size > 0);
    DCHECK(size < Thread::ThreadStackSize());
    uintptr_t b = a + size;
    if (is_new_mem)
      ClearMemoryStateOnStackNewMem(a, b);
    else 
      ClearMemoryStateOnStackDieMem(a, b);
    if(G_flags->sample_events) {
      static EventSampler sampler;
      sampler.Sample(tid, "SampleStackChange");
    }
  }

  // LOCK_BEFORE
  void HandleLockBefore() {
    if (G_flags->verbosity >= 2) {
      e_->Print();
    //  thr->ReportStackTrace();
    }
    cur_thread_->HandleLockBefore(e_->a());
  }

  void HandleLock(bool is_w_lock) {
    if (G_flags->verbosity >= 2) {
      e_->Print();
      cur_thread_->ReportStackTrace();
    }
    cur_thread_->HandleLock(is_w_lock);
  }

  // UNLOCK
  void HandleUnlock() {
    if (G_flags->verbosity >= 2) {
      e_->Print();
      cur_thread_->ReportStackTrace();
    }
    cur_thread_->HandleUnlock(e_->a());
  }


  void HandleLockCreateOrDestroy() {
    uintptr_t lock_addr = e_->a();
    if (e_->type() == LOCK_CREATE) {
      Lock::Create(lock_addr);
    } else {
      CHECK(e_->type() == LOCK_DESTROY);
      // A locked pthread_mutex_t can not be destroyed but other lock types can.
      // When destroying a lock, we must unlock it.
      // If there is a bug in a program when someone attempts to unlock
      // a destoyed lock, we are likely to fail in an assert.
      //
      // We do not unlock-on-destroy after main() has exited. 
      // This is because global Mutex objects may be desctructed while threads
      // holding them are still running. Urgh...
      Lock *lock = Lock::Lookup(lock_addr);
      CHECK(lock);
      if (lock->wr_held() || lock->rd_held()) {
        if (G_flags->unlock_on_mutex_destroy && !g_has_exited_main) {
          cur_thread_->HandleUnlock(lock_addr);
        }
      }
      Lock::Destroy(lock_addr);
    }
  }


  void HandleBusLock(bool is_acquire) {
    if (G_flags->verbosity >= 3) {
      e_->Print();
    }
    // TODO: do something smarter with atomics.
    if (is_acquire) {
//      cur_thread_->HandleLock(kTheBusLock, true);
      cur_thread_->set_bus_lock_is_set(true);
    } else {
//      cur_thread_->HandleUnlock(kTheBusLock);
      cur_thread_->set_bus_lock_is_set(false);
    }
  }


  // SIGNAL
  void HandleSignal() {
    if (G_flags->verbosity >= 2) {
      e_->Print();
    }
    cur_thread_->HandleSignal(e_->a());
  }
  // WAIT_BEFORE
  void HandleWaitBefore() {
    if (G_flags->verbosity >= 2) {
      e_->Print();
      cur_thread_->ReportStackTrace();
    }
    cur_thread_->HandleWaitBefore(e_->a(), e_->info());
  }

  // WAIT_AFTER, TWAIT_AFTER
  void HandleWaitAfter(bool timed_out) {
    if (G_flags->verbosity >= 2) {
      e_->Print();
    }
    // e_->Print();
    cur_thread_->HandleWaitAfter(timed_out);
  }

  // BARRIER_BEFORE
  void HandleBarrierBefore() {
    cur_thread_->HandleBarrierBefore(e_->a());
  }

  // BARRIER_AFTER
  void HandleBarrierAfter() {
    cur_thread_->HandleBarrierAfter();
  }

  void HandleTraceMem() {
    TID tid = cur_tid_;
    uintptr_t a = e_->a();
    CacheLine *cache_line = G_cache->GetLine(a, __LINE__);
    uintptr_t offset = CacheLine::ComputeOffset(a);
    cache_line->traced().Set(offset);
    if (G_flags->verbosity >= 2) e_->Print();
  }

  INLINE void RefAndUnrefTwoSegSetsIfDifferent(SSID new_ssid, SSID old_ssid) {
    if (new_ssid != old_ssid) {
      if (!new_ssid.IsEmpty()) {
        SegmentSet::Ref(new_ssid, "RefAndUnrefTwoSegSetsIfDifferent");
      }
      if (!old_ssid.IsEmpty()) {
        SegmentSet::Unref(old_ssid, "RefAndUnrefTwoSegSetsIfDifferent");
      }
    }
  }


  // return true if the current pair of read/write segment sets 
  // describes a race.
  bool NOINLINE CheckIfRace(SSID rd_ssid, SSID wr_ssid) { 
    int wr_ss_size = SegmentSet::Size(wr_ssid);
    int rd_ss_size = SegmentSet::Size(rd_ssid);

    DCHECK(wr_ss_size >= 2 || (wr_ss_size >= 1 && rd_ss_size >= 1));

    // check all write-write pairs
    for (int w1 = 0; w1 < wr_ss_size; w1++) {
      SID w1_sid = SegmentSet::GetSID(wr_ssid, w1, __LINE__);
      Segment *w1_seg = Segment::Get(w1_sid);
      LSID w1_ls = w1_seg->lsid(true);
      for (int w2 = w1 + 1; w2 < wr_ss_size; w2++) {
        DCHECK(wr_ssid.IsTuple());
        SegmentSet *ss = SegmentSet::Get(wr_ssid);
        LSID w2_ls = Segment::Get(ss->GetSID(w2))->lsid(true);
        if (LockSet::IntersectionIsEmpty(w1_ls, w2_ls)) {
          return true;
        }
      }
      // check all write-read pairs
      for (int r = 0; r < rd_ss_size; r++) {
        SID r_sid = SegmentSet::GetSID(rd_ssid, r, __LINE__);
        Segment *r_seg = Segment::Get(r_sid);
        LSID r_ls = r_seg->lsid(false);
        if (Segment::HappensBeforeOrSameThread(w1_sid, r_sid)) 
          continue;
        if (LockSet::IntersectionIsEmpty(w1_ls, r_ls)) {
          return true;
        }
      }
    }
    return false;
  }

  // New experimental state machine.
  // Set *res to the new state.
  // Return true if the new state is race.
  bool INLINE MemoryStateMachine(ShadowValue old_sval, Thread *thr, 
                                 bool is_w, bool tracing, ShadowValue *res) {
    ShadowValue new_sval;
    SID cur_sid = thr->sid();
    DCHECK(cur_sid.valid());

    if (old_sval.IsNew()) {
      // We see this memory for the first time. 
      DCHECK(cur_sid.valid());
      Segment::Ref(cur_sid, "MSM (new mem)");
      if (is_w) {
        new_sval.set(SSID(0), SSID(cur_sid));
      } else {
        new_sval.set(SSID(cur_sid), SSID(0));
      }
      *res = new_sval;
      return false;
    }

    SSID old_rd_ssid = old_sval.rd_ssid();
    SSID old_wr_ssid = old_sval.wr_ssid();
    SSID new_rd_ssid(0);
    SSID new_wr_ssid(0);
    if (is_w) {
      new_rd_ssid = SegmentSet::RemoveSegmentFromSS(old_rd_ssid, cur_sid);
      new_wr_ssid = SegmentSet::AddSegmentToSS(old_wr_ssid, cur_sid);
    } else {
      new_rd_ssid = SegmentSet::AddSegmentToSS(old_rd_ssid, cur_sid);
      new_wr_ssid = old_wr_ssid; 
    }

    if (UNLIKELY(G_flags->sample_events > 0)) {
      if (new_rd_ssid.IsTuple() || new_wr_ssid.IsTuple()) {
        static EventSampler sampler;
        sampler.Sample(thr->tid(), "HasTupleSS");
      }
    }


    new_sval.set(new_rd_ssid, new_wr_ssid);
    *res = new_sval;

    if (new_wr_ssid.IsTuple() || 
        (!new_wr_ssid.IsEmpty() && !new_rd_ssid.IsEmpty())) {
      return CheckIfRace(new_rd_ssid, new_wr_ssid);
    }
    return false;
  }
 

  INLINE ShadowValue HandleMemoryAccessHelper(bool is_w, 
                                              CacheLine *cache_line,
                                              uintptr_t addr, 
                                              uintptr_t size,
                                              TID tid, 
                                              Thread *thr
                                             ) {
    uintptr_t offset = CacheLine::ComputeOffset(addr);
    ShadowValue new_sval;

    bool tracing = false;
    if (UNLIKELY(G_flags->trace_level >= 1)) {
      if (UNLIKELY(cache_line->traced().Get(offset))) {
        tracing = true;
      } else if (UNLIKELY(addr == G_flags->trace_addr)) {
        tracing = true;
      }
    }

    ShadowValue *sval_p = cache_line->GetValuePointer(offset);
    ShadowValue old_sval;
    if (!cache_line->used().Get(offset)) {
      cache_line->used().Set(offset);
      cache_line->published().Clear(offset);
      old_sval.Clear();
    } else {
      old_sval = *sval_p;
    }

    bool is_published = cache_line->published().Get(offset);
    // We check only the first bit for publishing, oh well.
    
    if (UNLIKELY(is_published)) {
      const VTS *signaller_vts = GetPublisherVTS(addr);
      CHECK(signaller_vts);
      thr->NewSegmentForWait(signaller_vts);
    }

    bool is_race = MemoryStateMachine(old_sval, thr, is_w, tracing, &new_sval);

    if (UNLIKELY(tracing)) {
      Printf("TRACE: T%d %s[%d] addr=%p sval: %s%s\n", 
             tid.raw(), is_w ? "wr" : "rd", 
             size, addr, new_sval.ToString().c_str(),
             is_published ? " P" : "");
      thr->ReportStackTrace(GetVgPcOfCurrentThread());
    }

    // Check for race.
    if (UNLIKELY(is_race)) {
      if (G_flags->report_races && !cache_line->racey().Get(offset)) {
        reports_.AddReport(tid, GetVgPcOfCurrentThread(), is_w, addr, size, 
                           old_sval, new_sval, is_published);
      }
      // new_sval.set_racey(true);
      cache_line->racey().Set(offset);
    }   


    // Ref/Unref segments
    RefAndUnrefTwoSegSetsIfDifferent(new_sval.rd_ssid(),
                                     old_sval.rd_ssid());
    RefAndUnrefTwoSegSetsIfDifferent(new_sval.wr_ssid(),
                                     old_sval.wr_ssid());

    *sval_p = new_sval;


    return new_sval;
  }

  // return true if we can skip the memory access
  // due to fast mode. AddSegmentToSS the creator_tid()
  INLINE bool FastModeCheckAndUpdateCreatorTid(CacheLine *cache_line, TID tid) {
    // See
    // http://code.google.com/p/data-race-test/wiki/ThreadSanitizerAlgorithm#Fast_mode
    // for the details.

    if (!G_flags->fast_mode) return false;
    const TID kInvalidTID = TID();
    TID creator_tid = cache_line->creator_tid();
    if (cache_line->used().Empty() && creator_tid == kInvalidTID) {
      // we see this cache line for the first time (since it has been cleared).
      cache_line->set_creator_tid(tid);
      G_stats->fast_mode_first_time++;
      return true;
    } else if (creator_tid == tid) {
      // We are still in the same tid. Just return.
      DCHECK(cache_line->used().Empty());
      G_stats->fast_mode_still_in_creator++;
      return true;
    } else if (creator_tid != kInvalidTID) {
      // we are transitioning from the creator tid.
      DCHECK(cache_line->used().Empty());
      cache_line->set_creator_tid(kInvalidTID);
      G_stats->fast_mode_transition++;
    } else {
      DCHECK(!cache_line->used().Empty());
      DCHECK(creator_tid == kInvalidTID);
      G_stats->fast_mode_mt++;
    }
    return false;
  }


  INLINE void HandleMemoryAccessInternal(TID tid, 
                                         uintptr_t addr, uintptr_t size, 
                                         bool is_w) {


    if (UNLIKELY(G_flags->sample_events > 0)) {
      const char *type = 
          (cur_thread_->ignore(true) || cur_thread_->ignore(false))
          ? "SampleMemoryAccessIgnored"
          : "SampleMemoryAccess";
      static EventSampler sampler;
      sampler.Sample(tid, type);
    }

    Thread *thr = Thread::Get(tid);
    if (thr->ignore(is_w)) return;
    if (thr->bus_lock_is_set()) return;
    if (UNLIKELY(g_so_far_only_one_thread)) return;

    DCHECK(thr->lsid(false) == thr->segment()->lsid(false));
    DCHECK(thr->lsid(true) == thr->segment()->lsid(true));

    G_stats->memory_access_sizes_[size <= 16 ? size : 17 ]++;

    uintptr_t a = addr;
    uintptr_t b = a + size;

    CacheLine *cache_line = G_cache->GetLine(addr, __LINE__);
    
    // TODO(timurrrr): bug with unaligned access that touches two CacheLines.
    if (FastModeCheckAndUpdateCreatorTid(cache_line, tid)) return;

    if (UNLIKELY(G_flags->keep_history >= 2)) {
      // Keep the precise history. Very SLOW! 
      HandleSblockEnter(tid, GetVgPcOfCurrentThread());
    }

    if        (size == 8 && cache_line->SameValueStored(addr, 8)) {
      HandleMemoryAccessHelper(is_w, cache_line, addr, size, tid, thr);
      G_stats->n_access8++;
    } else if (size == 4 && cache_line->SameValueStored(addr, 4)) {
      HandleMemoryAccessHelper(is_w, cache_line, addr, size, tid, thr);
      G_stats->n_access4++;
    } else if (size == 2 && cache_line->SameValueStored(addr, 2)) {
      HandleMemoryAccessHelper(is_w, cache_line, addr, size, tid, thr);
      G_stats->n_access2++;
    } else if (size == 1) {
      HandleMemoryAccessHelper(is_w, cache_line, addr, size, tid, thr);
      G_stats->n_access1++;
    } else {
      // slow case 
      for (uintptr_t x = a; x < b; x++) {
        cache_line = G_cache->GetLine(x, __LINE__);
        if (FastModeCheckAndUpdateCreatorTid(cache_line, tid)) return;
        HandleMemoryAccessHelper(is_w, cache_line, x, size, tid, thr);
        G_stats->n_access_slow++;
      }
    }
  }

  void NOINLINE ClearMemoryStateOnMalloc(uintptr_t a, uintptr_t b) {
    ClearMemoryState(a, b, /*is_new_mem=*/true);
  }
  void NOINLINE ClearMemoryStateOnFree(uintptr_t a, uintptr_t b) {
    ClearMemoryState(a, b, /*is_new_mem=*/false);
  }

  // MALLOC
  void HandleMalloc() {
    TID tid = cur_tid_;
    uintptr_t a = e_->a();
    uintptr_t size = e_->info(); 

    if (G_flags->verbosity >= 2) {
      Printf("T%d MALLOC: %p %p %s %s\n", 
             tid.raw(), size, a,  
             Segment::ToString(cur_thread_->sid()).c_str(),
             cur_thread_->segment()->vts()->ToString().c_str()
             );
      // cur_thread_->ReportStackTrace(e_->pc());
    }

    uintptr_t b = a + size;
    ClearMemoryStateOnMalloc(a, b);
    // update heap_map
    HeapInfo info;
    info.ptr  = a;
    info.size = size;
    info.tid  = tid;
    // The current stack points to the function that called malloc().
    // We want to see the malloc() function itself at the top.
    // cur_thread_->PushCallStack(e_->pc());
    info.stack_trace = cur_thread_->CreateStackTrace();
    // cur_thread_->PopCallStack();

    // CHECK(!G_heap_map->count(a));  // we may have two calls 
                                      //  to AnnotateNewMemory.
    (*G_heap_map)[a] = info;
  }

  // FREE
  void HandleFree() {
    uintptr_t a = e_->a();
    if (G_flags->verbosity >= 2) {
      e_->Print();
    //  cur_thread_->ReportStackTrace(e_->pc());
    }
    if(!G_heap_map->count(a))
      return;
    // update G_heap_map
    // CHECK(G_heap_map->count(a));
    HeapInfo info = (*G_heap_map)[a];
    CHECK(info.ptr == a);
    StackTrace::Delete(info.stack_trace);
    G_heap_map->erase(a);

    uintptr_t size = info.size; 
    ClearMemoryStateOnFree(a, a + size);
  }

  void HandleThreadStart(TID child_tid, TID parent_tid, uintptr_t pc) {
    // Printf("HandleThreadStart: tid=%d parent_tid=%d pid=%d\n",
    //        child_tid.raw(), parent_tid.raw(), getpid());
    VTS *vts = NULL;
    StackTrace *creation_context = NULL;
    if (child_tid == TID(0)) {
      // main thread, we are done.
      vts = VTS::CreateSingleton(child_tid);
    } else {
      g_so_far_only_one_thread = false;
      Thread *parent = Thread::Get(parent_tid);
      VTS *parent_vts = parent->vts()->Clone();
      creation_context = parent->CreateStackTrace(pc);
      VTS *singleton = VTS::CreateSingleton(child_tid);
      vts = VTS::JoinAndTick(singleton, parent_vts, TID());
      VTS::Delete(singleton);
      VTS::Delete(parent_vts);

      parent->NewSegmentForSignal();

      if (G_flags->verbosity >= 2) {
        Printf("T%d:  THR_START   : %s %s\n", child_tid.raw(), 
               //Segment::ToString(sid).c_str(),
               parent->vts()->ToString().c_str(),
               vts->ToString().c_str()
              );
        if (G_flags->verbosity >= 2 && creation_context)
          Printf("%s", creation_context->ToString().c_str());
      }

    }

    Thread *new_thread = new Thread(child_tid, parent_tid, vts, creation_context);
    cur_thread_ = Thread::Get(child_tid);
    CHECK(new_thread == cur_thread_);
  }


  void HandleThreadCreateAfter() {
    Thread::SetThreadPthreadT(TID(e_->tid()),(pthread_t)e_->a());
  }

  // THR_END
  void HandleThreadEnd(TID tid) {
    // Printf("HandleThreadEnd: %d\n", tid.raw());
    if (tid != TID(0)) {
      Thread *child = Thread::Get(tid);
      child->HandleThreadEnd();

      if (G_flags->debug_level >= 3)
        Printf("T%d: end %p %p (%ld)\n", tid.raw(), 
             child->max_sp(), child->min_sp(), 
             child->max_sp() - child->min_sp());

      if (G_flags->verbosity >= 2) {
        Printf("T%d:  THR_END     : %s %s\n", tid.raw(), 
               Segment::ToString(child->sid()).c_str(),
               child->vts()->ToString().c_str()
               );
      }
    }
    Thread *thr = Thread::Get(tid);
    for (int rd_or_rw = 0; rd_or_rw <= 1; rd_or_rw++) {
      if (thr->ignore(rd_or_rw)) {
        Report("WARNING: T%d ended while the 'ignore %s' bit is set\n",
               rd_or_rw ? "writes" : "reads", tid.raw());
        if (G_flags->debug_level >= 1) {
          Report("Last ignore call was here: \n%s\n", 
                 thr->GetLastIgnoreContext(rd_or_rw)->ToString().c_str());
        }
      }
    }
    ShowProcSelfStatus();
  }

  // THR_JOIN_BEFORE
  void HandleThreadJoinBefore() {
    TID parent_tid = cur_tid_;;
    Thread *parent_thr = Thread::Get(parent_tid);
    parent_thr->HandleThreadJoinBefore((pthread_t)e_->a());
    if (G_flags->verbosity >= 2) {
      Printf("T%d:  THR_JOIN_BEFORE\n", 
             parent_tid.raw());
    }
  }

  // THR_JOIN_AFTER
  void HandleThreadJoinAfter() {
    TID tid = cur_tid_;
    Thread *parent_thr = Thread::Get(tid);
    VTS *vts_at_exit;
    TID child_tid = parent_thr->HandleThreadJoinAfter(&vts_at_exit);
    CHECK(vts_at_exit);
    CHECK(parent_thr->sid().valid());
    Segment::AssertLive(parent_thr->sid(),  __LINE__);
    parent_thr->NewSegmentForWait(vts_at_exit);
    if (G_flags->verbosity >= 2) {
      Printf("T%d:  THR_JOIN_AFTER T%d  : %s\n", tid.raw(), 
             child_tid.raw(), parent_thr->vts()->ToString().c_str());
    }
  }
  // data members
  Event *e_;
  TID cur_tid_;
  Thread *cur_thread_;

  ReportStorage reports_;
};

static Detector        *G_detector;

//--------- Flags ------------------------- {{{1
const char *usage_str = 
"Usage:\n"
"  %s [options] program_to_test [program's options]\n"
"See %s for details\n";
;


void ThreadSanitizerPrintUsage() {
  Printf(usage_str, TSAN_PROGRAM_NAME, TSAN_URL);
}

static void ReportUnknownFlagAndExit(const string &str) {
  Printf("Unknwon flag or flag value: %s\n", str.c_str());
  ThreadSanitizerPrintUsage();
  exit(1);
}

// if arg and flag match, return true
// and set 'val' to the substring of arg after '='.
static bool FlagNameMatch(const string &arg, const string &flag, string *val) {
  string f = string("--") + flag;
  if (arg.size() < f.size()) return false;
  for (size_t i = 0; i < f.size(); i++) {
    // '-' must match '-'
    // '_' may match '_' or '-'
    if (f[i] == '_') {
      if (arg[i] != '-' && arg[i] != '_') return false;
    } else {
      if (f[i] != arg[i]) return false;
    }
  }
  if (arg.size() == f.size()) {
    *val = "";
    return true;
  }
  if (arg[f.size()] != '=') return false;
  *val = arg.substr(f.size() + 1);
  return true;
}

static void FindBoolFlag(const char *name, bool default_val, 
                  vector<string> *args, bool *retval) {
  *retval = default_val;
  bool cont = false;
  do {
    cont = false;
    vector<string>::iterator it = args->begin();
    for (; it != args->end(); ++it) {
      string &str = *it;
      string flag_value;
      if (!FlagNameMatch(str, name, &flag_value)) continue;
      if (flag_value == "")            *retval = true;
      else if (flag_value == "1")     *retval = true;
      else if (flag_value == "true")  *retval = true;
      else if (flag_value == "yes")   *retval = true;
      else if (flag_value == "0")     *retval = false;
      else if (flag_value == "false") *retval = false;
      else if (flag_value == "no")    *retval = false;
      else ReportUnknownFlagAndExit(str);
      if (G_flags->verbosity >= 1) {
        Printf("%40s => %s\n", name, *retval ? "true" : "false");
      }
      break;
    }
    if (it != args->end()) {
      cont = true;
      args->erase(it);
    }
  } while(cont); 
}

static void FindIntFlag(const char *name, intptr_t default_val,
                 vector<string> *args, intptr_t *retval) {
  *retval = default_val;
  bool cont = false;
  do {
    cont = false;
    vector<string>::iterator it = args->begin();
    for (; it != args->end(); ++it) {
      string &str = *it;
      string flag_value;
      if (!FlagNameMatch(str, name, &flag_value)) continue;
      char *end_ptr;
      const char *beg_ptr = flag_value.c_str();
      intptr_t int_val = my_strtol(beg_ptr, &end_ptr);
      if (flag_value.empty() || beg_ptr + flag_value.size() != end_ptr) 
        ReportUnknownFlagAndExit(str);
      *retval = int_val;
      if (G_flags->verbosity >= 1) {
        Printf("%40s => %ld\n", name, *retval);
      }
      break;
    }
    if (it != args->end()) {
      cont = true;
      args->erase(it);
    }
  } while(cont); 
}

static void FindUIntFlag(const char *name, intptr_t default_val,
                 vector<string> *args, uintptr_t *retval) {
  intptr_t signed_int;
  FindIntFlag(name, default_val, args, &signed_int);
  CHECK(signed_int >= 0);
  *retval = signed_int;
}

void FindStringFlag(const char *name, vector<string> *args,
                    vector<string> *retval) {
  bool cont = false;
  do {
    cont = false;
    vector<string>::iterator it = args->begin();
    for (; it != args->end(); ++it) {
      string &str = *it;
      string flag_value;
      if (!FlagNameMatch(str, name, &flag_value)) continue;
      retval->push_back(flag_value);
      if (G_flags->verbosity >= 1) {
        Printf("%40s => %s\n", name, flag_value.c_str());
      }
      break;
    }
    if (it != args->end()) {
      cont = true;
      args->erase(it);
    }
  } while(cont); 
}

void ThreadSanitizerParseFlags(vector<string> &args) { 
  // Check this first.
  FindIntFlag("v", 0, &args, &G_flags->verbosity);

  FindBoolFlag("ignore_stack", true, &args, &G_flags->ignore_stack);
  FindIntFlag("keep_history", 1, &args, &G_flags->keep_history);
  FindUIntFlag("segment_set_recycle_queue_size", DEBUG_MODE ? 10 : 10000, &args,
               &G_flags->segment_set_recycle_queue_size);
  FindBoolFlag("fast_mode", true, &args, &G_flags->fast_mode);
  FindBoolFlag("pure_happens_before", false, &args, &G_flags->pure_happens_before);
  FindBoolFlag("show_expected_races", false, &args, 
               &G_flags->show_expected_races);
  FindBoolFlag("demangle", true, &args, &G_flags->demangle);

  FindBoolFlag("announce_threads", false, &args, &G_flags->announce_threads);
  FindBoolFlag("full_output", false, &args, &G_flags->full_output);
  FindBoolFlag("show_states", false, &args, &G_flags->show_states);
  FindBoolFlag("show_proc_self_status", false, &args, &G_flags->show_proc_self_status);
  FindBoolFlag("show_valgrind_context", false, &args, &G_flags->show_valgrind_context);
  FindBoolFlag("show_pc", false, &args, &G_flags->show_pc);
  FindBoolFlag("ignore_in_dtor", true, &args, &G_flags->ignore_in_dtor);
  FindBoolFlag("exit_after_main", false, &args, &G_flags->exit_after_main);

  FindBoolFlag("show_stats", false, &args, &G_flags->show_stats);
  FindBoolFlag("color", false, &args, &G_flags->color);
  FindBoolFlag("html", false, &args, &G_flags->html);

  FindIntFlag("dry_run", 0, &args, &G_flags->dry_run);
  FindBoolFlag("report_races", true, &args, &G_flags->report_races);
  FindBoolFlag("compress_cache_lines", false, &args, &G_flags->compress_cache_lines);
  FindBoolFlag("unlock_on_mutex_destroy", true, &args, &G_flags->unlock_on_mutex_destroy);

  FindIntFlag("sample_events", 0, &args, &G_flags->sample_events);
  FindIntFlag("sample_events_depth", 2, &args, &G_flags->sample_events_depth);

  FindIntFlag("debug_level", 1, &args, &G_flags->debug_level);
  FindIntFlag("trace_level", 0, &args, &G_flags->trace_level);


  FindStringFlag("file_prefix_to_cut", &args, &G_flags->file_prefix_to_cut);
  FindStringFlag("ignore", &args, &G_flags->ignore);

  FindBoolFlag("detect_thread_create", false, &args, &G_flags->detect_thread_create);

  FindIntFlag("trace_addr", 0, &args, (intptr_t*)&G_flags->trace_addr);

  vector<string> summary_file_tmp;
  FindStringFlag("summary_file", &args, &summary_file_tmp);
  if (summary_file_tmp.size() > 0) {
    G_flags->summary_file = summary_file_tmp.back();
  }


  FindIntFlag("max_sid", kMaxSID, &args, &G_flags->max_sid);
  kMaxSID = G_flags->max_sid;
  if (kMaxSID <= 100000) {
    Printf("Error: max-sid should be at least 100000. Exiting\n");
    exit(1);
  }


  FindIntFlag("num_callers_in_history", kSizeOfHistoryStackTrace, &args, 
              &G_flags->num_callers_in_history);
  kSizeOfHistoryStackTrace = G_flags->num_callers_in_history;
  if (G_flags->keep_history == 0) {
    kSizeOfHistoryStackTrace = 1;
  }


//  FindIntFlag("num_callers", 15, &args, &G_flags->num_callers);
  // we get num-callers from valgrind flags. 
  G_flags->num_callers = VG_(clo_backtrace_size);

  G_flags->max_n_threads        = 20000;

  if (G_flags->pure_happens_before) {
    // disable fast mode in pure happens-before mode.
    G_flags->fast_mode = false;
  }

  if (G_flags->full_output) {
    G_flags->ignore_in_dtor = false;
    G_flags->announce_threads = true;
    G_flags->show_pc = true;
    G_flags->show_states = true;
    G_flags->file_prefix_to_cut.clear();
  }


  if (!args.empty()) {
    ReportUnknownFlagAndExit(args.front());
  }
}

//--------- ThreadSanitizer ------------------ {{{1


// This function is taken from valgrind's m_libcbase.c (thanks GPL!).
static bool FastRecursiveStringMatch (const char* pat, const char* str, int *depth) {
  CHECK((*depth) < 10000);
  (*depth)++;
  for (;;) {
    switch (*pat) {
      case '\0':(*depth)--;
                return (*str=='\0');
      case '*': do {
                  if (FastRecursiveStringMatch(pat+1,str, depth)) {
                    (*depth)--;
                    return True;
                  }
                } while (*str++);
                  (*depth)--;
                  return False;
      case '?': if (*str++=='\0') {
                  (*depth)--;
                  return False;
                }
                pat++;
                break;
      case '\\':if (*++pat == '\0') {
                  (*depth)--;
                  return False; /* spurious trailing \ in pattern */
                }
                /* falls through to ... */
      default : if (*pat++ != *str++) {
                  (*depth)--;
                  return False;
                }
                break;
    }
  }
}
                                                                                                                                                                                                                                                                                                                                                                                                                                       

static bool StringMatch(const string &pattern, const string &str) {
  int depth = 0;
  return FastRecursiveStringMatch(pattern.c_str(), str.c_str(), &depth);
}

void SplitStringIntoLinesAndRemoveBlanksAndComments(
    const string &str, vector<string> *lines) {
  string cur_line;
  bool in_comment = false;
  for (size_t pos = 0; pos < str.size(); pos++) {
    char ch = str[pos];
    if (ch == '\n') {
      if (!cur_line.empty()) {
        // Printf("++ %s\n", cur_line.c_str());
        lines->push_back(cur_line);
      }
      cur_line.clear();
      in_comment = false;
      continue;
    }
    if (ch == ' ' || ch == '\t') continue;
    if (ch == '#' ) {
      in_comment = true;
      continue;
    }
    if (!in_comment) {
      cur_line += ch;
    }
  }
}

struct IgnoreLists {
  vector<string> funcs;
  vector<string> objs;
  vector<string> files;
};

static IgnoreLists *g_ignore_lists;


// Setup the list of functions/images/files to ignore.
static void SetupIgnore() {
  g_ignore_lists = new IgnoreLists;
  // add some major ignore entries so that tsan remains sane 
  // even w/o any ignore file.
  g_ignore_lists->objs.push_back("*/libpthread-*");
  g_ignore_lists->objs.push_back("*/ld-2*.so");
  g_ignore_lists->files.push_back("*ts_valgrind_intercepts.c");
  
  // Now read the ignore files.
  for (size_t i = 0; i < G_flags->ignore.size(); i++) {
    string file_name = G_flags->ignore[i];
    Report("INFO: Reading ignore file: %s\n", file_name.c_str());
    string str = ReadFileToString(file_name);
    vector<string> lines;
    SplitStringIntoLinesAndRemoveBlanksAndComments(str, &lines);
    for (size_t j = 0; j < lines.size(); j++) {
      string &line = lines[j];
      if (line.find("obj:") == 0) {
        string s = line.substr(4);
        g_ignore_lists->objs.push_back(s);
      }
      if (line.find("fun:") == 0) {
        string s = line.substr(4);
        g_ignore_lists->funcs.push_back(s);
      }
      if (line.find("src:") == 0) {
        string s = line.substr(4);
        g_ignore_lists->files.push_back(s);
      }
    }
  }
}

bool ThreadSanitizerWantToInstrumentSblock(uintptr_t pc) {
  string img_name, rtn_name, file_name;
  int line_no;
  G_stats->pc_to_strings++;
  PcToStrings(pc, false, &img_name, &rtn_name, &file_name, &line_no);

  for (size_t i = 0; i < g_ignore_lists->files.size(); i++) {
    if (StringMatch(g_ignore_lists->files[i], file_name)) 
      return False;
  }
  for (size_t i = 0; i < g_ignore_lists->objs.size(); i++) {
    if (StringMatch(g_ignore_lists->objs[i], img_name)) 
      return False;
  }
  for (size_t i = 0; i < g_ignore_lists->funcs.size(); i++) {
    if (StringMatch(g_ignore_lists->funcs[i], rtn_name)) 
      return False;
  }

  return True;
}


extern void ThreadSanitizerInit() {
  ScopedMallocCostCenter cc("ThreadSanitizerInit");
  g_so_far_only_one_thread = true;
  CHECK(sizeof(ShadowValue) == 8);
  CHECK(G_flags);
  G_stats        = new Stats;
  G_detector     = new Detector;
  G_cache        = new Cache;
  G_expected_races_map = new ExpectedRacesMap;
  G_heap_map           = new HeapMap;
  {
    ScopedMallocCostCenter cc1("Segment::InitClassMembers");
    Segment::InitClassMembers();
  }
  SegmentSet::InitClassMembers();
  CacheLine::InitClassMembers();
  Thread::InitClassMembers();
  Lock::InitClassMembers();
  LockSet::InitClassMembers();
  EventSampler::InitClassMembers();
  VTS::InitClassMembers();
  // TODO(timurrrr): make sure *::InitClassMembers() are called only once for
  // each class
  g_publish_info_map = new PublishInfoMap;
  g_stack_trace_free_list = new StackTraceFreeList;
  g_pcq_map = new PCQMap;

  SetupIgnore();

  if (G_flags->html) {
    c_bold    = "<font ><b>";
    c_red     = "<font color=red><b>";
    c_green   = "<font color=green><b>";
    c_magenta = "<font color=magenta><b>";
    c_cyan    = "<font color=cyan><b>";
    c_blue   = "<font color=blue><b>";
    c_yellow  = "<font color=yellow><b>";
    c_default = "</b></font>";
  } else if (G_flags->color) {
    // Enable ANSI colors. 
    c_bold    = "\033[1m";
    c_red     = "\033[31m";
    c_green   = "\033[32m";
    c_yellow  = "\033[33m";
    c_blue    = "\033[34m";
    c_magenta = "\033[35m";
    c_cyan    = "\033[36m";
    c_default = "\033[0m";
  }


  if (G_flags->verbosity >= 1) {
    Report("INFO: Started pid %d\n",  getpid());
  }
}

extern void ThreadSanitizerFini() {
  G_detector->HandleProgramEnd();
}

extern void ThreadSanitizerHandleOneEvent(Event *event) {
  G_detector->HandleOneEvent(event);
}

void INLINE ThreadSanitizerHandleMemoryAccess(int32_t tid, 
                                              uintptr_t addr, uintptr_t size, 
                                              bool is_w) {
  G_detector->HandleMemoryAccess(tid, addr, size, is_w);
}


void INLINE ThreadSanitizerHandleStackMemChange(int32_t tid, uintptr_t addr, 
                                                uintptr_t size, bool is_new) {

  G_detector->HandleStackMemChange(tid, addr, size, is_new);
}


void INLINE ThreadSanitizerEnterSblock(int32_t tid, uintptr_t pc) {
  G_detector->HandleSblockEnter(TID(tid), pc);
}


void INLINE ThreadSanitizerHandleRtnCall(int32_t tid, uintptr_t call_pc, 
                                         uintptr_t target_pc) {
  G_detector->HandleRtnCall(TID(tid), call_pc, target_pc);
}
void INLINE ThreadSanitizerHandleRtnExit(int32_t tid) {
  G_detector->HandleRtnExit(TID(tid));
}

extern void ThreadSanitizerPrintReport(ThreadSanitizerReport *report) {
  ReportStorage::PrintReport(report);
}

//--------- ts_inst_valgrind.cc -------------------------- {{{1
// gcc doesnt't have cross-file inlining, so do it ourselves, 
// but only in optimized build.
#if not defined(DEBUG)
#define TS_INSTR_VALGRIND_HERE
#include "ts_valgrind.cc"
#endif


//--------- TODO -------------------------- {{{1
// - Support configurable aliases for function names (is it doable in valgrind)?
// - Correctly support atomic operations (not just ignore).
// - Implement initialization state. (do we need it if we have fast-mode?)
// - Handle INC as just one write
//   - same for memset, etc
// - Implement correct handling of memory accesses with different sizes.
// - Do not create HB arcs between RdUnlock and RdLock
// - Compress cache lines 
// - Optimize the case where a threads signals twice in a row on the same
//   address.
// - Fix --ignore-in-dtor if --demangle=no.
// end. {{{1
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
