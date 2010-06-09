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

// You can find the details on this tool at
// http://code.google.com/p/data-race-test

#ifndef INCLUDE_THREAD_SANITIZER_CC

#include "thread_sanitizer.h"
#include "suppressions.h"
#include <stdarg.h>
// -------- Constants --------------- {{{1
// Segment ID (SID)      is in range [1, kMaxSID-1]
// Segment Set ID (SSID) is in range [-kMaxSID+1, -1]
// This is not a compile-time constant, but it can only be changed at startup.
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

// -------- Globals --------------- {{{1

bool g_has_expensive_flags = false;
bool g_so_far_only_one_thread = false;
bool g_has_entered_main = false;
bool g_has_exited_main = false;

size_t g_last_flush_time;

// Incremented on each Lock and Unlock. Used by LockHistory.
uint32_t g_lock_era = 0;

FLAGS *G_flags = NULL;

bool g_race_verifier_active = false;

bool debug_expected_races = false;
bool debug_malloc = false;
bool debug_free = false;
bool debug_thread = false;
bool debug_ignore = false;
bool debug_rtn = false;
bool debug_lock = false;
bool debug_wrap = false;
bool debug_ins = false;
bool debug_shadow_stack = false;
bool debug_happens_before = false;
bool debug_cache = false;
bool debug_race_verifier = false;

// -------- Util ----------------------------- {{{1

string PcToRtnNameWithStats(uintptr_t pc, bool demangle) {
  G_stats->pc_to_rtn_name++;
  return PcToRtnName(pc, demangle);
}

static string RemovePrefixFromString(string str) {
  for (size_t i = 0; i < G_flags->file_prefix_to_cut.size(); i++) {
    string prefix_to_cut = G_flags->file_prefix_to_cut[i];
    size_t pos = str.find(prefix_to_cut);
    if (pos != string::npos) {
      str = str.substr(pos + prefix_to_cut.size());
    }
  }
  if (str.find("./") == 0) {  // remove leading ./
    str = str.substr(2);
  }
  return str;
}

string PcToRtnNameAndFilePos(uintptr_t pc) {
  G_stats->pc_to_strings++;
  string img_name;
  string file_name;
  string rtn_name;
  int line_no = -1;
  PcToStrings(pc, G_flags->demangle, &img_name, &rtn_name,
              &file_name, &line_no);
  file_name = RemovePrefixFromString(file_name);
  if (file_name == "") {
    return rtn_name + " " + RemovePrefixFromString(img_name);
  }
  char buff[10];
  snprintf(buff, sizeof(buff), "%d", line_no);
  return rtn_name + " " + file_name + ":" + buff;
}

// -------- ID ---------------------- {{{1
// We wrap int32_t into ID class and then inherit various ID type from ID.
// This is done in an attempt to implement type safety of IDs, i.e.
// to make it impossible to make implicit cast from one ID type to another.
class ID {
 public:
  typedef int32_t T;
  explicit ID(T id) : id_(id) {}
  ID(const ID &id) : id_(id.id_) {}
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
  LID GetSingleton() const { return LID(raw()); }
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
  SID  GetSingleton() const {
    DCHECK(IsSingleton());
    return SID(raw());
  }
  // TODO(timurrrr): need to start SegmentSetArray indices from 1
  // to avoid "int ???() { return -raw() - 1; }"
};

// -------- Colors ----------------------------- {{{1
// Colors for ansi terminals and for html.
const char *c_bold    = "";
const char *c_red     = "";
const char *c_green   = "";
const char *c_magenta = "";
const char *c_cyan    = "";
const char *c_blue    = "";
const char *c_yellow  = "";
const char *c_default = "";


// -------- Simple Cache ------ {{{1
#include "ts_simple_cache.h"
// -------- PairCache & IntPairToIntCache ------ {{{1
template <typename A, typename B, typename Ret,
         int kHtableSize, int kArraySize = 8>
class PairCache {
 public:
  PairCache() {
    CHECK(kHtableSize >= 0);
    CHECK(sizeof(Entry) == sizeof(A) + sizeof(B) + sizeof(Ret));
    Flush();
  }

  void Flush() {
    memset(this, 0, sizeof(*this));

    // Change the first hashtable entry so it doesn't match (0,0) on Lookup.
    if (kHtableSize != 0)
      memset(&htable_[0], 1, sizeof(Entry));

    // Any Lookup should fail now.
    for (int i = 0; i < kHtableSize; i++) {
      Ret tmp;
      DCHECK(!Lookup(htable_[i].a, htable_[i].b, &tmp));
    }
    CHECK(array_pos_    == 0);
    CHECK(array_filled_ == false);
  }

  void Insert(A a, B b, Ret v) {
    // fill the hash table
    if (kHtableSize != 0) {
      uint32_t idx  = compute_idx(a, b);
      htable_[idx].Fill(a, b, v);
    }

    // fill the array
    Ret dummy;
    if (kArraySize != 0 && !ArrayLookup(a, b, &dummy)) {
      array_[array_pos_ % kArraySize].Fill(a, b, v);
      array_pos_ = (array_pos_ + 1) % kArraySize;
      if (array_pos_ > kArraySize)
        array_filled_ = true;
    }
  }

  bool Lookup(A a, B b, Ret *v) {
    // check the array
    if (kArraySize != 0 && ArrayLookup(a, b, v)) {
      G_stats->ls_cache_fast++;
      return true;
    }
    // check the hash table.
    if (kHtableSize != 0) {
      uint32_t idx  = compute_idx(a, b);
      Entry & prev_e = htable_[idx];
      if (prev_e.Match(a, b)) {
        *v = prev_e.v;
        return true;
      }
    }
    return false;
  }

 private:
  struct Entry {
    A a;
    B b;
    Ret v;
    void Fill(A a, B b, Ret v) {
      this->a = a;
      this->b = b;
      this->v = v;
    }
    bool Match(A a, B b) const {
      return this->a == a && this->b == b;
    }
  };

  bool ArrayLookup(A a, B b, Ret *v) {
    for (int i = 0; i < (array_filled_ ? kArraySize : array_pos_); i++) {
      Entry & entry = array_[i];
      if (entry.Match(a, b)) {
        *v = entry.v;
        return true;
      }
    }
    return false;
  }

  uint32_t compute_idx(A a, B b) {
    if (kHtableSize == 0)
      return 0;
    else
      return combine2(a, b) % kHtableSize;
  }

  static uint32_t combine2(int a, int b) {
    return (a << 16) ^ b;
  }

  static uint32_t combine2(SSID a, SID b) {
    return combine2(a.raw(), b.raw());
  }

  Entry htable_[kHtableSize];

  Entry array_[kArraySize];

  // array_pos_    - next element to write to the array_ (mod kArraySize)
  // array_filled_ - set to true once we write the last element of the array
  int array_pos_;
  bool array_filled_;
};

template<int kHtableSize, int kArraySize = 8>
class IntPairToIntCache
  : public PairCache<int, int, int, kHtableSize, kArraySize> {};



// -------- FreeList --------------- {{{1
class FreeList {
 public:
  FreeList(int obj_size, int chunk_size)
    : list_(0),
      obj_size_(obj_size),
      chunk_size_(chunk_size) {
    CHECK_GE(obj_size_, static_cast<int>(sizeof(NULL)));
    CHECK((obj_size_ % sizeof(NULL)) == 0);
    CHECK_GE(chunk_size_, 1);
  }

  void *Allocate() {
    if (!list_)
      AllocateNewChunk();
    CHECK(list_);
    List *head = list_;
    list_ = list_->next;
    return reinterpret_cast<void*>(head);
  }

  void Deallocate(void *ptr) {
    if (DEBUG_MODE) {
      memset(ptr, 0xac, obj_size_);
    }
    List *new_head = reinterpret_cast<List*>(ptr);
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
      List *new_head = reinterpret_cast<List*>(new_mem + obj_size_ * i);
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
// -------- StackTrace -------------- {{{1
class StackTraceFreeList {
 public:
  uintptr_t *GetNewMemForStackTrace(size_t capacity) {
    DCHECK(capacity <= (size_t)G_flags->num_callers);
    return reinterpret_cast<uintptr_t*>(free_lists_[capacity]->Allocate());
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
  FreeList **free_lists_;  // Array of G_flags->num_callers lists.
};

static StackTraceFreeList *g_stack_trace_free_list;

class StackTrace {
 public:
  static StackTrace *CreateNewEmptyStackTrace(size_t size,
                                              size_t capacity = 0) {
    ScopedMallocCostCenter cc("StackTrace::CreateNewEmptyStackTrace()");
    DCHECK(g_stack_trace_free_list);
    DCHECK(size != 0);
    if (capacity == 0)
      capacity = size;
    uintptr_t *mem = g_stack_trace_free_list->GetNewMemForStackTrace(capacity);
    DCHECK(mem);
    StackTrace *res = new(mem) StackTrace(size, capacity);
    return res;
  }

  static void Delete(StackTrace *trace) {
    if (!trace) return;
    DCHECK(g_stack_trace_free_list);
    g_stack_trace_free_list->TakeStackTraceBack(
        reinterpret_cast<uintptr_t*>(trace), trace->capacity());
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

  static bool CutStackBelowFunc(const string func_name) {
    for (size_t i = 0; i < G_flags->cut_stack_below.size(); i++) {
      if (StringMatch(G_flags->cut_stack_below[i], func_name)) {
        return true;
      }
    }
    return false;
  }

  static string EmbeddedStackTraceToString(const uintptr_t *emb_trace, size_t n,
                                           const char *indent = "    ") {
    string res = "";
    for (size_t i = 0; i < n; i++) {
      const int kBuffSize = 10000;
      char buff[kBuffSize];
      if (!emb_trace[i]) break;
      string rtn_and_file = PcToRtnNameAndFilePos(emb_trace[i]);
      string rtn = PcToRtnName(emb_trace[i], true);

      if (i == 0) res += c_bold;
      if (G_flags->show_pc) {
        snprintf(buff, kBuffSize, "%s#%-2d %p: ",
                 indent, static_cast<int>(i),
                 reinterpret_cast<void*>(emb_trace[i]));
      } else {
        snprintf(buff, kBuffSize, "%s#%-2d ", indent, static_cast<int>(i));
      }
      res += buff;

      res += rtn_and_file;
      if (i == 0) res += c_default;
      res += "\n";

      // don't print after main ...
      if (rtn_and_file.find("main ") == 0)
        break;
      // ... and after some default functions (see ThreadSanitizerParseFlags())
      // and some more functions specified via command line flag.
      if (CutStackBelowFunc(rtn))
        break;
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

  struct Less {
    bool operator() (const StackTrace *t1, const StackTrace *t2) const {
      size_t size = min(t1->size_, t2->size_);
      for (size_t i = 0; i < size; i++) {
        if (t1->arr_[i] != t2->arr_[i])
          return (t1->arr_[i] < t2->arr_[i]);
      }
      return t1->size_ < t2->size_;
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



// -------- Lock -------------------- {{{1
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

  void set_name(const char *name) { name_ = name; }
  const char *name() const { return name_; }

  string ToString() const {
    string res;
    char buff[100];
    snprintf(buff, sizeof(buff), "L%d", lid_.raw());
    // do we need to print the address?
    // reinterpret_cast<void*>(lock_addr()));
    res = buff;
    if (name()) {
      res += string(" ") + name();
    }
    return res;
  }

  static Lock *LIDtoLock(LID lid) {
    // slow, but needed only for reports.
    for (Map::iterator it = map_->begin(); it != map_->end(); ++it) {
      Lock *l = it->second;
      if (l->lid_ == lid) {
        return l;
      }
    }
    return NULL;
  }

  static string ToString(LID lid) {
    Lock *lock = LIDtoLock(lid);
    CHECK(lock);
    return lock->ToString();
  }

  static void ReportLockWithOrWithoutContext(LID lid, bool with_context) {
    if (!with_context) {
      Report("   L%d\n", lid.raw());
      return;
    }
    Lock *lock = LIDtoLock(lid);
    CHECK(lock);
    if (lock->last_lock_site_) {
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
      last_lock_site_(0),
      name_(NULL) {
  }

  // Data members
  uintptr_t lock_addr_;
  LID       lid_;
  int       rd_held_;
  int       wr_held_;
  bool      is_pure_happens_before_;
  StackTrace *last_lock_site_;
  const char *name_;

  // Static members
  typedef map<uintptr_t, Lock*> Map;
  static Map *map_;
};


Lock::Map *Lock::map_;

// Returns a string like "L123,L234".
static string SetOfLocksToString(const set<LID> &locks) {
  string res;
  for (set<LID>::const_iterator it = locks.begin();
       it != locks.end(); ++it) {
    LID lid = *it;
    char buff[100];
    snprintf(buff, sizeof(buff), "L%d", lid.raw());
    if (it != locks.begin())
      res += ", ";
    res += buff;
  }
  return res;
}

// -------- FixedArray--------------- {{{1
template <typename T, size_t SizeLimit = 1024>
class FixedArray {
 public:
  explicit FixedArray(size_t array_size)
      : size_(array_size),
        array_((array_size <= SizeLimit
                ? alloc_space_
                : new T[array_size])) { }

  ~FixedArray() {
    if (array_ != alloc_space_) {
      delete[] array_;
    }
  }

  T* begin() { return array_; }
  T& operator[](int i)             { return array_[i]; }

 private:
  const size_t size_;
  T* array_;
  T alloc_space_[SizeLimit];
};

// -------- LockSet ----------------- {{{1
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
      set.insert(lsid.GetSingleton());
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
      if (lsid.GetSingleton() != lid) return false;
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

    // LockSets are equal and not empty
    if (lsid1 == lsid2)
      return false;

    // both are not singletons - slow path.
    bool ret = true,
         cache_hit = false;
    DCHECK(lsid2.raw() < 0);
    if (ls_intersection_cache_->Lookup(lsid1.raw(), -lsid2.raw(), &ret)) {
      if (!DEBUG_MODE)
        return ret;
      cache_hit = true;
    }
    const LSSet &set1 = Get(lsid1);
    const LSSet &set2 = Get(lsid2);

    FixedArray<LID> intersection(min(set1.size(), set2.size()));
    LID *end = set_intersection(set1.begin(), set1.end(),
                            set2.begin(), set2.end(),
                            intersection.begin());
    DCHECK(!cache_hit || (ret == (end == intersection.begin())));
    ret = (end == intersection.begin());
    ls_intersection_cache_->Insert(lsid1.raw(), -lsid2.raw(), ret);
    return ret;
  }

  static string ToString(LSID lsid) {
    if (lsid.IsEmpty()) {
      return "{}";
    } else if (lsid.IsSingleton()) {
      return "{" + Lock::ToString(lsid.GetSingleton()) + "}";
    }
    const LSSet &set = Get(lsid);
    string res = "{";
    for (LSSet::const_iterator it = set.begin(); it != set.end(); ++it) {
      if (it != set.begin()) res += ", ";
      res += Lock::ToString(*it);
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
      LID lid = lsid.GetSingleton();
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
      locks->insert(lsid.GetSingleton());
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
    ls_intersection_cache_ = new LSIntersectionCache;
  }

 private:
  // No instances are allowed.
  LockSet() { }

  typedef multiset<LID> LSSet;

  static LSSet &Get(LSID lsid) {
    ScopedMallocCostCenter cc(__FUNCTION__);
    int idx = -lsid.raw() - 1;
    DCHECK(idx >= 0);
    DCHECK(idx < static_cast<int>(vec_->size()));
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
    ScopedMallocCostCenter cc(__FUNCTION__);
    if (*id == 0) {
      vec_->push_back(set);
      *id = map_->size();
    }
    return LSID(-*id);
  }

  typedef map<LSSet, int32_t> Map;
  static Map *map_;

  static const char *kLockSetVecAllocCC;
  typedef vector<LSSet> Vec;
  static Vec *vec_;

//  static const int kPrimeSizeOfLsCache = 307;
//  static const int kPrimeSizeOfLsCache = 499;
  static const int kPrimeSizeOfLsCache = 1021;
  typedef IntPairToIntCache<kPrimeSizeOfLsCache> LSCache;
  static LSCache *ls_add_cache_;
  static LSCache *ls_rem_cache_;
  static LSCache *ls_int_cache_;
  typedef IntPairToBoolCache<kPrimeSizeOfLsCache> LSIntersectionCache;
  static LSIntersectionCache *ls_intersection_cache_;
};

LockSet::Map *LockSet::map_;
LockSet::Vec *LockSet::vec_;
const char *LockSet::kLockSetVecAllocCC = "kLockSetVecAllocCC";
LockSet::LSCache *LockSet::ls_add_cache_;
LockSet::LSCache *LockSet::ls_rem_cache_;
LockSet::LSCache *LockSet::ls_int_cache_;
LockSet::LSIntersectionCache *LockSet::ls_intersection_cache_;


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




// -------- VTS ------------------ {{{1
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
    VTS *res = new(mem) VTS(size);
    G_stats->vts_total_size += size;
    return res;
  }

  // TODO(timurrrr): rename Delete/Clone with Unref/Ref
  static void Delete(VTS *vts) {
    if (!vts) return;
    CHECK_GT(vts->ref_count_, 0);
    vts->ref_count_--;
    if (vts->ref_count_ == 0) {
      G_stats->vts_delete++;
      size_t size = vts->size_;  // can't use vts->size().
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

  static VTS *Join(const VTS *vts_a, const VTS *vts_b) {
    CHECK(vts_a->ref_count_);
    CHECK(vts_b->ref_count_);
    FixedArray<TS> result_ts(vts_a->size() + vts_b->size());
    TS *t = result_ts.begin();
    const TS *a = &vts_a->arr_[0];
    const TS *b = &vts_b->arr_[0];
    const TS *a_max = a + vts_a->size();
    const TS *b_max = b + vts_b->size();
    while (a < a_max && b < b_max) {
      if (a->tid < b->tid) {
        *t = *a;
        a++;
        t++;
      } else if (a->tid > b->tid) {
        *t = *b;
        b++;
        t++;
      } else {
        if (a->clk >= b->clk) {
          *t = *a;
        } else {
          *t = *b;
        }
        a++;
        b++;
        t++;
      }
    }
    while (a < a_max) {
      *t = *a;
      a++;
      t++;
    }
    while (b < b_max) {
      *t = *b;
      b++;
      t++;
    }

    VTS *res = VTS::Create(t - result_ts.begin());
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
      snprintf(buff, sizeof(buff), "%d:%d;", arr_[i].tid.raw(), arr_[i].clk);
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
    // TODO(kcc): need more tests here...
    const char *test_vts[] = {
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
        Printf("HB = %d\n   %s\n   %s\n", static_cast<int>(hb),
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

    VTS *v12 = Join(v1, v2);
    v12->print("v12");
    VTS *v34 = Join(v3, v4);
    v34->print("v34");

    VTS *x1 = Parse("[0:4; 3:6; 4:2;]");
    CHECK(x1);
    x1->print("x1");
    TestHappensBefore();
  }

  // Parse VTS string in the form "[0:4; 3:6; 4:2;]".
  static VTS *Parse(const char *str) {
#if 1  // TODO(kcc): need sscanf in valgrind
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

  int32_t uniq_id() const { return uniq_id_; }

 private:
  explicit VTS(size_t size)
    : ref_count_(1),
      size_(size) {
    uniq_id_counter_++;
    // If we've got overflow, we are in trouble, need to have 64-bits...
    CHECK_GT(uniq_id_counter_, 0);
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

  static const size_t kNumberOfFreeLists = 512;  // Must be power of two.
//  static const size_t kNumberOfFreeLists = 64; // Must be power of two.
  static FreeList **free_lists_;  // Array of kNumberOfFreeLists elements.
};

int32_t VTS::uniq_id_counter_;
VTS::HBCache *VTS::hb_cache_;
FreeList **VTS::free_lists_;



// -------- Mask -------------------- {{{1
// A bit mask (32-bits on 32-bit arch and 64-bits on 64-bit arch).
class Mask {
 public:
  static const uintptr_t kOne = 1;
  static const uintptr_t kNBits = sizeof(uintptr_t) * 8;
  static const uintptr_t kNBitsLog = kNBits == 32 ? 5 : 6;

  Mask() : m_(0) {}
  Mask(const Mask &m) : m_(m.m_) { }
  explicit Mask(uintptr_t m) : m_(m) { }
  bool Get(uintptr_t idx) const   { return m_ & (kOne << idx); }
  void Set(uintptr_t idx)   { m_ |= kOne << idx; }
  void Clear(uintptr_t idx) { m_ &= ~(kOne << idx); }
  bool Empty() const {return m_ == 0; }

  // Clear bits in range [a,b) and return old [a,b) range.
  Mask ClearRangeAndReturnOld(uintptr_t a, uintptr_t b) {
    DCHECK(a < b);
    DCHECK(b <= kNBits);
    uintptr_t res;
    uintptr_t n_bits_in_mask = (b - a);
    if (n_bits_in_mask == kNBits) {
      res = m_;
      m_ = 0;
    } else {
      uintptr_t t = (kOne << n_bits_in_mask);
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
    DCHECK(b <= kNBits);
    uintptr_t n_bits_in_mask = (b - a);
    if (n_bits_in_mask == kNBits) {
      m_ = ~0;
    } else {
      uintptr_t t = (kOne << n_bits_in_mask);
      uintptr_t mask = (t - 1) << a;
      m_ |= mask;
    }
  }

  uintptr_t GetRange(uintptr_t a, uintptr_t b) const {
    // a bug was fixed here
    DCHECK(a < b);
    DCHECK(b <= kNBits);
    uintptr_t n_bits_in_mask = (b - a);
    if (n_bits_in_mask == kNBits) {
      return m_;
    } else {
      uintptr_t t = (kOne << n_bits_in_mask);
      uintptr_t mask = (t - 1) << a;
      return m_ & mask;
    }
  }

  void Subtract(Mask m) { m_ &= ~m.m_; }
  void Union(Mask m) { m_ |= m.m_; }

  static Mask Intersection(Mask m1, Mask m2) { return Mask(m1.m_ & m2.m_); }


  void Clear() { m_ = 0; }


  string ToString() const {
    char buff[kNBits+1];
    for (uintptr_t i = 0; i < kNBits; i++) {
      buff[i] = Get(i) ? '1' : '0';
    }
    buff[kNBits] = 0;
    return buff;
  }

  static void Test() {
    Mask m;
    m.Set(2);
    Printf("%s\n", m.ToString().c_str());
    m.ClearRange(0, kNBits);
    Printf("%s\n", m.ToString().c_str());
  }

 private:
  uintptr_t m_;
};

// -------- Segment -------------------{{{1
class Segment {
 public:
  // for debugging...
  static bool ProfileSeg(SID sid) {
    // return (sid.raw() % (1 << 14)) == 0;
    return false;
  }

  // non-static methods

  VTS *vts() const { return vts_; }
  TID tid() const { return TID(tid_); }
  LSID  lsid(bool is_w) const { return lsid_[is_w]; }
  uint32_t lock_era() const { return lock_era_; }

  // static methods

  static uintptr_t *embedded_stack_trace(SID sid) {
    DCHECK(sid.valid());
    DCHECK(kSizeOfHistoryStackTrace > 0);
    size_t chunk_idx = (unsigned)sid.raw() / kChunkSizeForStacks;
    size_t idx       = (unsigned)sid.raw() % kChunkSizeForStacks;
    DCHECK(chunk_idx < n_stack_chunks_);
    DCHECK(all_stacks_[chunk_idx] != NULL);
    return &all_stacks_[chunk_idx][idx * kSizeOfHistoryStackTrace];
  }

  static void ensure_space_for_stack_trace(SID sid) {
    DCHECK(sid.valid());
    DCHECK(kSizeOfHistoryStackTrace > 0);
    size_t chunk_idx = (unsigned)sid.raw() / kChunkSizeForStacks;
    DCHECK(chunk_idx < n_stack_chunks_);
    if (all_stacks_[chunk_idx])
      return;
    for (size_t i = 0; i <= chunk_idx; i++) {
      if (all_stacks_[i]) continue;
      all_stacks_[i] = new uintptr_t[
          kChunkSizeForStacks * kSizeOfHistoryStackTrace];
      // we don't clear this memory, it will be clreared later lazily.
      // We also never delete it because it will be used until the very end.
    }
  }

  static string StackTraceString(SID sid) {
    DCHECK(kSizeOfHistoryStackTrace > 0);
    return StackTrace::EmbeddedStackTraceToString(
        embedded_stack_trace(sid), kSizeOfHistoryStackTrace);
  }

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

      if (ProfileSeg(SID(n_segments_))) {
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
      if (ProfileSeg(sid)) {
       Printf("Segment: reused SID %d\n", sid.raw());
      }
    }
    DCHECK(seg);
    seg->ref_count_ = 0;
    seg->tid_ = tid;
    seg->lsid_[0] = rd_lockset;
    seg->lsid_[1] = wr_lockset;
    seg->vts_ = vts;
    seg->lock_era_ = g_lock_era;
    if (kSizeOfHistoryStackTrace > 0) {
      ensure_space_for_stack_trace(sid);
      embedded_stack_trace(sid)[0] = 0;
    }
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
    const size_t kRecyclePeriod = 10000;  // TODO(kcc): test it!
    Segment *seg = GetInternal(sid);
    DCHECK(seg->ref_count_ == 0);
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
    if (ProfileSeg(sid)) {
      Printf("Segment: recycled SID %d\n", sid.raw());
    }
    return true;
  }

  int ref_count() const {return ref_count_; }

  static void INLINE Ref(SID sid, const char *where) {
    Segment *seg = GetInternal(sid);
    if (ProfileSeg(sid)) {
      Printf("SegRef   : %d ref=%d %s; tid=%d\n", sid.raw(),
             seg->ref_count_, where, seg->tid().raw());
    }
    DCHECK(seg->ref_count_ >= 0);
    seg->ref_count_++;
  }
  static void INLINE Unref(SID sid, const char *where) {
    Segment *seg = GetInternal(sid);
    if (ProfileSeg(sid)) {
      Printf("SegUnref : %d ref=%d %s\n", sid.raw(), seg->ref_count_, where);
    }
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
    snprintf(buff, sizeof(buff), "T%d/S%d", Get(sid)->tid().raw(), sid.raw());
    return buff;
  }

  static string ToStringTidOnly(SID sid) {
    char buff[100];
    snprintf(buff, sizeof(buff), "T%d", Get(sid)->tid().raw());
    return buff;
  }

  static string ToStringWithLocks(SID sid) {
    char buff[100];
    Segment *seg = Get(sid);
    snprintf(buff, sizeof(buff), "T%d/S%d ", seg->tid().raw(), sid.raw());
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

  static void ShowSegmentStats() {
    Printf("Segment::ShowSegmentStats:\n");
    Printf("n_segments_: %d\n", n_segments_);
    Printf("reusable_sids_: %ld\n", reusable_sids_->size());
    Printf("recycled_sids_: %ld\n", recycled_sids_->size());
    map<int, int> ref_to_freq_map;
    for (int i = 1; i < n_segments_; i++) {
      Segment *seg = GetInternal(SID(i));
      int refcount = seg->ref_count_;
      if (refcount > 10) refcount = 10;
      ref_to_freq_map[refcount]++;
    }
    for (map<int, int>::iterator it = ref_to_freq_map.begin();
         it != ref_to_freq_map.end(); ++it) {
      Printf("ref %d => freq %d\n", it->first, it->second);
    }
  }

  static void InitClassMembers() {
    if (G_flags->keep_history == 0)
      kSizeOfHistoryStackTrace = 0;
    Report("INFO: Allocating %ldMb (%ld * %ldM) for Segments.\n",
           (sizeof(Segment) * kMaxSID) >> 20,
           sizeof(Segment), kMaxSID >> 20);
    if (kSizeOfHistoryStackTrace) {
      Report("INFO: Will allocate up to %ldMb for 'previous' stack traces.\n",
             (kSizeOfHistoryStackTrace * sizeof(uintptr_t) * kMaxSID) >> 20);
    }

    all_segments_  = new Segment[kMaxSID];
    // initialization all segments to 0.
    memset(all_segments_, 0, kMaxSID * sizeof(Segment));
    // initialize all_segments_[0] with garbage
    memset(all_segments_, -1, sizeof(Segment));

    if (kSizeOfHistoryStackTrace > 0) {
      n_stack_chunks_ = kMaxSID / kChunkSizeForStacks;
      if (n_stack_chunks_ * kChunkSizeForStacks < (size_t)kMaxSID)
        n_stack_chunks_++;
      all_stacks_ = new uintptr_t*[n_stack_chunks_];
      memset(all_stacks_, 0, sizeof(uintptr_t*) * n_stack_chunks_);
    }
    n_segments_    = 1;
    reusable_sids_ = new vector<SID>;
    recycled_sids_ = new vector<SID>;
  }

 private:
  static Segment *GetSegmentByIndex(int32_t index) {
    return &all_segments_[index];
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
  uint32_t lock_era_; /* followed by a 32 bit padding on 64-bit arch :( */

  // static class members.

  // One large array of segments. The size is set by a command line (--max-sid)
  // and never changes. Once we are out of vacant segments, we flush the state.
  static Segment *all_segments_;
  // We store stack traces separately because their size is unknown
  // at compile time and because they are needed less often.
  // The stacks are stored as an array of chunks, instead of one array, 
  // so that for small tests we do not require too much RAM.
  // We don't use vector<> or another resizable array to avoid expensive 
  // resizing.
  enum { kChunkSizeForStacks = DEBUG_MODE ? 512 : 4 * 1024 * 1024 };
  static uintptr_t **all_stacks_;
  static size_t      n_stack_chunks_;

  static int32_t n_segments_;
  static vector<SID> *reusable_sids_;
  static vector<SID> *recycled_sids_;
};

Segment          *Segment::all_segments_;
uintptr_t       **Segment::all_stacks_;
size_t            Segment::n_stack_chunks_;
int32_t           Segment::n_segments_;
vector<SID>      *Segment::reusable_sids_;
vector<SID>      *Segment::recycled_sids_;

// -------- SegmentSet -------------- {{{1
class SegmentSet {
 public:
  static NOINLINE SSID AddSegmentToSS(SSID old_ssid, SID new_sid);
  static NOINLINE SSID RemoveSegmentFromSS(SSID old_ssid, SID sid_to_remove);

  static INLINE SSID AddSegmentToTupleSS(SSID ssid, SID new_sid);
  static INLINE SSID RemoveSegmentFromTupleSS(SSID old_ssid, SID sid_to_remove);

  SSID ComputeSSID() {
    SSID res = map_->GetIdOrZero(this);
    CHECK_NE(res.raw(), 0);
    return res;
  }

  int ref_count() const { return ref_count_; }

  static void AssertLive(SSID ssid, int line) {
    DCHECK(ssid.valid());
    if (DEBUG_MODE) {
      if (ssid.IsSingleton()) {
        Segment::AssertLive(ssid.GetSingleton(), line);
      } else {
        DCHECK(ssid.IsTuple());
        int idx = -ssid.raw()-1;
        DCHECK(idx < static_cast<int>(vec_->size()));
        DCHECK(idx >= 0);
        SegmentSet *res = (*vec_)[idx];
        DCHECK(res);
        DCHECK(res->ref_count_ >= 0);
        res->Validate(line);

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
    DCHECK(idx < static_cast<int>(vec_->size()) && idx >= 0);
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
    DCHECK(idx < static_cast<int>(vec_->size()) && idx >= 0);
    CHECK((*vec_)[idx] == this);
    // Printf("SegmentSet::RecycleOneSegmentSet: %d\n", ssid.raw());
    //
    // Recycle segments
    for (int i = 0; i < kMaxSegmentSetSize; i++) {
      SID sid = this->GetSID(i);
      if (sid.raw() == 0) break;
      Segment::Unref(sid, "SegmentSet::Recycle");
    }
    ref_count_ = -1;

    map_->Erase(this);
    ready_to_be_reused_->push_back(ssid);
    G_stats->ss_recycle++;
  }

  static void INLINE Ref(SSID ssid, const char *where) {
    DCHECK(ssid.valid());
    if (ssid.IsSingleton()) {
      Segment::Ref(ssid.GetSingleton(), where);
    } else {
      SegmentSet *sset = Get(ssid);
      // Printf("SSRef   : %d ref=%d %s\n", ssid.raw(), sset->ref_count_, where);
      DCHECK(sset->ref_count_ >= 0);
      sset->ref_count_++;
    }
  }

  static void INLINE Unref(SSID ssid, const char *where) {
    DCHECK(ssid.valid());
    if (ssid.IsSingleton()) {
      Segment::Unref(ssid.GetSingleton(), where);
    } else {
      SegmentSet *sset = Get(ssid);
      // Printf("SSUnref : %d ref=%d %s\n", ssid.raw(), sset->ref_count_, where);
      DCHECK(sset->ref_count_ > 0);
      sset->ref_count_--;
      if (sset->ref_count_ == 0) {
        // We don't delete unused SSID straightaway due to performance reasons
        // (to avoid flushing caches too often and because SSID may be reused
        // again soon)
        //
        // Instead, we use two queues (deques):
        //    ready_to_be_recycled_ and ready_to_be_reused_.
        // The algorithm is following:
        // 1) When refcount_ becomes zero, we push the SSID into
        //    ready_to_be_recycled_.
        // 2) When ready_to_be_recycled_ becomes too large, we call
        //    FlushRecycleQueue().
        //    In FlushRecycleQueue(), we pop the first half of
        //    ready_to_be_recycled_ and for each popped SSID we do
        //     * if "refcount_ > 0", do nothing (this SSID is in use again)
        //     * otherwise, we recycle this SSID (delete its VTS, etc) and push
        //       it into ready_to_be_reused_
        // 3) When a new SegmentSet is about to be created, we re-use SSID from
        //    ready_to_be_reused_ (if available)
        ready_to_be_recycled_->push_back(ssid);
        if (UNLIKELY(ready_to_be_recycled_->size() >
                     2 * G_flags->segment_set_recycle_queue_size)) {
          FlushRecycleQueue();
        }
      }
    }
  }

  static void FlushRecycleQueue() {
    while (ready_to_be_recycled_->size() >
        G_flags->segment_set_recycle_queue_size) {
      SSID rec_ssid = ready_to_be_recycled_->front();
      ready_to_be_recycled_->pop_front();
      int idx = -rec_ssid.raw()-1;
      SegmentSet *rec_ss = (*vec_)[idx];
      DCHECK(rec_ss);
      DCHECK(rec_ss == Get(rec_ssid));
      // We should check that this SSID haven't been referenced again.
      if (rec_ss->ref_count_ == 0) {
        rec_ss->RecycleOneSegmentSet(rec_ssid);
      }
    }

    // SSIDs will be reused soon - need to flush some caches.
    FlushCaches();
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

  static void FlushCaches() {
    add_segment_cache_->Flush();
    remove_segment_cache_->Flush();
  }

  static void ForgetAllState() {
    for (size_t i = 0; i < vec_->size(); i++) {
      delete (*vec_)[i];
    }
    map_->Clear();
    vec_->clear();
    ready_to_be_reused_->clear();
    ready_to_be_recycled_->clear();
    FlushCaches();
  }


  static void Test();

  static int32_t Size(SSID ssid) {
    if (ssid.IsEmpty()) return 0;
    if (ssid.IsSingleton()) return 1;
    return Get(ssid)->size();
  }

  SID GetSID(int32_t i) const {
    DCHECK(i >= 0 && i < kMaxSegmentSetSize);
    DCHECK(i == 0 || sids_[i-1].raw() != 0);
    return sids_[i];
  }

  void SetSID(int32_t i, SID sid) {
    DCHECK(i >= 0 && i < kMaxSegmentSetSize);
    DCHECK(i == 0 || sids_[i-1].raw() != 0);
    sids_[i] = sid;
  }

  static SID GetSID(SSID ssid, int32_t i, int line) {
    DCHECK(ssid.valid());
    if (ssid.IsSingleton()) {
      DCHECK(i == 0);
      Segment::AssertLive(ssid.GetSingleton(), line);
      return ssid.GetSingleton();
    } else {
      AssertLive(ssid, __LINE__);
      SID sid = Get(ssid)->GetSID(i);
      Segment::AssertLive(sid, line);
      return sid;
    }
  }

  static bool INLINE Contains(SSID ssid, SID seg) {
    if (LIKELY(ssid.IsSingleton())) {
      return ssid.GetSingleton() == seg;
    } else if (LIKELY(ssid.IsEmpty())) {
      return false;
    }

    SegmentSet *ss = Get(ssid);
    for (int i = 0; i < kMaxSegmentSetSize; i++) {
      SID sid = ss->GetSID(i);
      if (sid.raw() == 0) break;
      if (sid == seg)
        return true;
    }
    return false;
  }

  static Segment *GetSegmentForNonSingleton(SSID ssid, int32_t i, int line) {
    return Segment::Get(GetSID(ssid, i, line));
  }

  void NOINLINE Validate(int line) const;

  static size_t NumberOfSegmentSets() { return vec_->size(); }


  static void InitClassMembers() {
    map_    = new Map;
    vec_    = new vector<SegmentSet *>;
    ready_to_be_recycled_ = new deque<SSID>;
    ready_to_be_reused_ = new deque<SSID>;
    add_segment_cache_ = new SsidSidToSidCache;
    remove_segment_cache_ = new SsidSidToSidCache;
  }

 private:
  SegmentSet()  // Private CTOR
    : ref_count_(0) {
    // sids_ are filled with zeroes due to SID default CTOR.
    if (DEBUG_MODE) {
      for (int i = 0; i < kMaxSegmentSetSize; i++)
        CHECK_EQ(sids_[i].raw(), 0);
    }
  }

  int size() const {
    for (int i = 0; i < kMaxSegmentSetSize; i++) {
      if (sids_[i].raw() == 0) {
        CHECK_GE(i, 2);
        return i;
      }
    }
    return kMaxSegmentSetSize;
  }

  static INLINE SSID AllocateAndCopy(SegmentSet *ss) {
    DCHECK(ss->ref_count_ == 0);
    DCHECK(sizeof(int32_t) == sizeof(SID));
    SSID res_ssid;
    SegmentSet *res_ss = 0;

    if (!ready_to_be_reused_->empty()) {
      res_ssid = ready_to_be_reused_->front();
      ready_to_be_reused_->pop_front();
      int idx = -res_ssid.raw()-1;
      res_ss = (*vec_)[idx];
      DCHECK(res_ss);
      DCHECK(res_ss->ref_count_ == -1);
      G_stats->ss_reuse++;
      for (int i = 0; i < kMaxSegmentSetSize; i++) {
        res_ss->sids_[i] = SID(0);
      }
    } else {
      // create a new one
      ScopedMallocCostCenter cc("SegmentSet::CreateNewSegmentSet");
      G_stats->ss_create++;
      res_ss = new SegmentSet;
      vec_->push_back(res_ss);
      res_ssid = SSID(-((int32_t)vec_->size()));
      CHECK(res_ssid.valid());
    }
    DCHECK(res_ss);
    res_ss->ref_count_ = 0;
    for (int i = 0; i < kMaxSegmentSetSize; i++) {
      SID sid = ss->GetSID(i);
      if (sid.raw() == 0) break;
      Segment::Ref(sid, "SegmentSet::FindExistingOrAlocateAndCopy");
      res_ss->SetSID(i, sid);
    }
    DCHECK(res_ss == Get(res_ssid));
    map_->Insert(res_ss, res_ssid);
    return res_ssid;
  }

  static NOINLINE SSID FindExistingOrAlocateAndCopy(SegmentSet *ss) {
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
    SegmentSet tmp;
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
  struct Less {
    INLINE bool operator() (const SegmentSet *ss1,
                            const SegmentSet *ss2) const {
      for (int i = 0; i < kMaxSegmentSetSize; i++) {
        SID sid1 = ss1->sids_[i],
            sid2 = ss2->sids_[i];
        if (sid1 != sid2) return sid1 < sid2;
      }
      return false;
    }
  };

  struct SSEq {
    INLINE bool operator() (const SegmentSet *ss1,
                            const SegmentSet *ss2) const {
      G_stats->sseq_calls++;

      for (int i = 0; i < kMaxSegmentSetSize; i++) {
        SID sid1 = ss1->sids_[i],
            sid2 = ss2->sids_[i];
        if (sid1 != sid2) return false;
      }
      return true;
    }
  };

  struct SSHash {
    INLINE size_t operator() (const SegmentSet *ss) const {
      uintptr_t res = 0;
      uint32_t* sids_array = (uint32_t*)ss->sids_;
      // We must have even number of SIDs.
      DCHECK((kMaxSegmentSetSize % 2) == 0);

      G_stats->sshash_calls++;
      // xor all SIDs together, half of them bswap-ed.
      for (int i = 0; i < kMaxSegmentSetSize; i += 2) {
        uintptr_t t1 = sids_array[i];
        uintptr_t t2 = sids_array[i+1];
        if (t2) t2 = tsan_bswap(t2);
        res = res ^ t1 ^ t2;
      }
      return res;
    }
  };

  struct SSTraits {
    enum {
      // These values are taken from the hash_compare defaults.
      bucket_size = 4,  // Must be greater than zero.
      min_buckets = 8,  // Must be power of 2.
    };

    INLINE size_t operator()(const SegmentSet *ss) const {
      SSHash sshash;
      return sshash(ss);
    }

    INLINE bool operator()(const SegmentSet *ss1, const SegmentSet *ss2) const {
      Less less;
      return less(ss1, ss2);
    }
  };

  template <class MapType>
  static SSID GetIdOrZeroFromMap(MapType *map, SegmentSet *ss) {
    typename MapType::iterator it = map->find(ss);
    if (it == map->end())
      return SSID(0);
    return it->second;
  }

  class Map {
   public:
    SSID GetIdOrZero(SegmentSet *ss) {
      return GetIdOrZeroFromMap(&map_, ss);
    }

    void Insert(SegmentSet *ss, SSID id) {
      map_[ss] = id;
    }

    void Erase(SegmentSet *ss) {
      CHECK(map_.erase(ss));
    }

    void Clear() {
      map_.clear();
    }

   private:
    // TODO(timurrrr): consider making a custom hash_table.
#if defined(_MSC_VER)
    typedef hash_map<SegmentSet*, SSID, SSTraits > MapType__;
#elif 1
    typedef hash_map<SegmentSet*, SSID, SSHash, SSEq > MapType__;
#else
    // Old code, may be useful for debugging.
    typedef map<SegmentSet*, SSID, Less > MapType__;
#endif
    MapType__ map_;
  };

//  typedef map<SegmentSet*, SSID, Less> Map;

  static Map                  *map_;
  // TODO(kcc): use vector<SegmentSet> instead.
  static vector<SegmentSet *> *vec_;
  static deque<SSID>         *ready_to_be_reused_;
  static deque<SSID>         *ready_to_be_recycled_;

  typedef PairCache<SSID, SID, SSID, 1009, 1> SsidSidToSidCache;
  static SsidSidToSidCache    *add_segment_cache_;
  static SsidSidToSidCache    *remove_segment_cache_;

  // sids_ contains up to kMaxSegmentSetSize SIDs.
  // Contains zeros at the end if size < kMaxSegmentSetSize.
  SID     sids_[kMaxSegmentSetSize];
  int32_t ref_count_;
};

SegmentSet::Map      *SegmentSet::map_;
vector<SegmentSet *> *SegmentSet::vec_;
deque<SSID>         *SegmentSet::ready_to_be_reused_;
deque<SSID>         *SegmentSet::ready_to_be_recycled_;
SegmentSet::SsidSidToSidCache    *SegmentSet::add_segment_cache_;
SegmentSet::SsidSidToSidCache    *SegmentSet::remove_segment_cache_;




SSID SegmentSet::RemoveSegmentFromSS(SSID old_ssid, SID sid_to_remove) {
  DCHECK(old_ssid.IsValidOrEmpty());
  DCHECK(sid_to_remove.valid());
  SSID res;
  if (remove_segment_cache_->Lookup(old_ssid, sid_to_remove, &res)) {
    return res;
  }

  if (old_ssid.IsEmpty()) {
    res = old_ssid;  // Nothing to remove.
  } else if (LIKELY(old_ssid.IsSingleton())) {
    SID sid = old_ssid.GetSingleton();
    if (Segment::HappensBeforeOrSameThread(sid, sid_to_remove))
      res = SSID(0);  // Empty.
    else
      res = old_ssid;
  } else {
    res = RemoveSegmentFromTupleSS(old_ssid, sid_to_remove);
  }
  remove_segment_cache_->Insert(old_ssid, sid_to_remove, res);
  return res;
}


// static
//
// This method returns a SSID of a SegmentSet containing "new_sid" and all those
// segments from "old_ssid" which do not happen-before "new_sid".
//
// For details, see
// http://code.google.com/p/data-race-test/wiki/ThreadSanitizerAlgorithm#State_machine
SSID SegmentSet::AddSegmentToSS(SSID old_ssid, SID new_sid) {
  DCHECK(old_ssid.raw() == 0 || old_ssid.valid());
  DCHECK(new_sid.valid());
  Segment::AssertLive(new_sid, __LINE__);
  SSID res;

  // These two TIDs will only be used if old_ssid.IsSingleton() == true.
  TID old_tid;
  TID new_tid;

  if (LIKELY(old_ssid.IsSingleton())) {
    SID old_sid(old_ssid.raw());
    DCHECK(old_sid.valid());
    Segment::AssertLive(old_sid, __LINE__);

    if (UNLIKELY(old_sid == new_sid)) {
      // The new segment equals the old one - nothing has changed.
      return old_ssid;
    }

    old_tid = Segment::Get(old_sid)->tid();
    new_tid = Segment::Get(new_sid)->tid();
    if (LIKELY(old_tid == new_tid)) {
      // The new segment is in the same thread - just replace the SID.
      return SSID(new_sid);
    }

    if (Segment::HappensBefore(old_sid, new_sid)) {
      // The new segment is in another thread, but old segment
      // happens before the new one - just replace the SID.
      return SSID(new_sid);
    }

    DCHECK(!Segment::HappensBefore(new_sid, old_sid));
    // The only other case is Signleton->Doubleton transition, see below.
  } else if (LIKELY(old_ssid.IsEmpty())) {
    return SSID(new_sid);
  }

  // Lookup the cache.
  if (add_segment_cache_->Lookup(old_ssid, new_sid, &res)) {
    SegmentSet::AssertLive(res, __LINE__);
    return res;
  }

  if (LIKELY(old_ssid.IsSingleton())) {
    // Signleton->Doubleton transition.
    // These two TIDs were initialized before cache lookup (see above).
    DCHECK(old_tid.valid());
    DCHECK(new_tid.valid());

    SID old_sid(old_ssid.raw());
    DCHECK(old_sid.valid());

    DCHECK(!Segment::HappensBefore(new_sid, old_sid));
    DCHECK(!Segment::HappensBefore(old_sid, new_sid));
    res = (old_tid < new_tid
      ? DoubletonSSID(old_sid, new_sid)
      : DoubletonSSID(new_sid, old_sid));
    SegmentSet::AssertLive(res, __LINE__);
  } else {
    res = AddSegmentToTupleSS(old_ssid, new_sid);
    SegmentSet::AssertLive(res, __LINE__);
  }

  // Put the result into cache.
  add_segment_cache_->Insert(old_ssid, new_sid, res);

  return res;
}

SSID SegmentSet::RemoveSegmentFromTupleSS(SSID ssid, SID sid_to_remove) {
  DCHECK(ssid.IsTuple());
  DCHECK(ssid.valid());
  AssertLive(ssid, __LINE__);
  SegmentSet *ss = Get(ssid);

  int32_t old_size = 0, new_size = 0;
  SegmentSet tmp;
  SID * tmp_sids = tmp.sids_;
  CHECK(sizeof(int32_t) == sizeof(SID));

  for (int i = 0; i < kMaxSegmentSetSize; i++, old_size++) {
    SID sid = ss->GetSID(i);
    if (sid.raw() == 0) break;
    DCHECK(sid.valid());
    Segment::AssertLive(sid, __LINE__);
    if (Segment::HappensBeforeOrSameThread(sid, sid_to_remove))
      continue;  // Skip this segment from the result.
    tmp_sids[new_size++] = sid;
  }

  if (new_size == old_size) return ssid;
  if (new_size == 0) return SSID(0);
  if (new_size == 1) return SSID(tmp_sids[0]);

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

  Segment::AssertLive(new_sid, __LINE__);
  const Segment *new_seg = Segment::Get(new_sid);
  TID            new_tid = new_seg->tid();

  int32_t old_size = 0, new_size = 0;
  SID tmp_sids[kMaxSegmentSetSize + 1];
  CHECK(sizeof(int32_t) == sizeof(SID));
  bool inserted_new_sid = false;
  // traverse all SID in current ss. tids are ordered.
  for (int i = 0; i < kMaxSegmentSetSize; i++, old_size++) {
    SID sid = ss->GetSID(i);
    if (sid.raw() == 0) break;
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
      if (seg->vts() == new_seg->vts()) {
        // Optimization: if a segment with the same VTS as the current is
        // already inside SS, don't modify the SS.
        // Improves performance with --keep-history >= 1.
        return ssid;
      }
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

  CHECK_GT(new_size, 0);
  if (new_size == 1) {
    return SSID(new_sid.raw());  // Singleton.
  }

  if (new_size > kMaxSegmentSetSize) {
    CHECK(new_size == kMaxSegmentSetSize + 1);
    // we need to forget one segment. Which? The oldest one.
    int seg_to_forget = 0;
    Segment *oldest_segment = NULL;
    for (int i = 0; i < new_size; i++) {
      SID sid = tmp_sids[i];
      if (sid == new_sid) continue;
      Segment *s = Segment::Get(tmp_sids[i]);
      if (oldest_segment == NULL ||
          oldest_segment->vts()->uniq_id() > s->vts()->uniq_id()) {
        oldest_segment = s;
        seg_to_forget = i;
      }
    }
    DCHECK(oldest_segment);

    // Printf("seg_to_forget: %d T%d\n", tmp_sids[seg_to_forget].raw(),
    //        oldest_segment->tid().raw());
    for (int i = seg_to_forget; i < new_size - 1; i++) {
      tmp_sids[i] = tmp_sids[i+1];
    }
    new_size--;
  }

  CHECK(new_size <= kMaxSegmentSetSize);
  SegmentSet tmp;
  for (int i = 0; i < new_size; i++)
    tmp.sids_[i] = tmp_sids[i];  // TODO(timurrrr): avoid copying?
  if (DEBUG_MODE) tmp.Validate(__LINE__);

  SSID res = FindExistingOrAlocateAndCopy(&tmp);
  if (DEBUG_MODE) Get(res)->Validate(__LINE__);
  return res;
}



void NOINLINE SegmentSet::Validate(int line) const {
  // This is expensive!
  int my_size = size();
  for (int i = 0; i < my_size; i++) {
    SID sid1 = GetSID(i);
    CHECK(sid1.valid());
    Segment::AssertLive(sid1, __LINE__);

    for (int j = i + 1; j < my_size; j++) {
      SID sid2 = GetSID(j);
      CHECK(sid2.valid());
      Segment::AssertLive(sid2, __LINE__);

      bool hb1 = Segment::HappensBefore(sid1, sid2);
      bool hb2 = Segment::HappensBefore(sid2, sid1);
      if (hb1 || hb2) {
        Printf("BAD at line %d: %d %d %s %s\n   %s\n   %s\n",
               line, static_cast<int>(hb1), static_cast<int>(hb2),
               Segment::ToString(sid1).c_str(),
               Segment::ToString(sid2).c_str(),
               Segment::Get(sid1)->vts()->ToString().c_str(),
               Segment::Get(sid2)->vts()->ToString().c_str());
      }
      CHECK(!Segment::HappensBefore(GetSID(i), GetSID(j)));
      CHECK(!Segment::HappensBefore(GetSID(j), GetSID(i)));
      CHECK(Segment::Get(sid1)->tid() < Segment::Get(sid2)->tid());
    }
  }

  for (int i = my_size; i < kMaxSegmentSetSize; i++) {
    CHECK_EQ(sids_[i].raw(), 0);
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
    SID sid = GetSID(i);
    if (i) res += ", ";
    CHECK(sid.valid());
    Segment::AssertLive(sid, __LINE__);
    res += Segment::ToStringTidOnly(sid).c_str();
  }
  res += "}";
  return res;
}

// static
void SegmentSet::Test() {
  LSID ls(0);  // dummy
  SID sid1 = Segment::AddNewSegment(TID(0), VTS::Parse("[0:2;]"), ls, ls);
  SID sid2 = Segment::AddNewSegment(TID(1), VTS::Parse("[0:1; 1:1]"), ls, ls);
  SID sid3 = Segment::AddNewSegment(TID(2), VTS::Parse("[0:1; 2:1]"), ls, ls);
  SID sid4 = Segment::AddNewSegment(TID(3), VTS::Parse("[0:1; 3:1]"), ls, ls);
  SID sid5 = Segment::AddNewSegment(TID(4), VTS::Parse("[0:3; 2:2; 3:2;]"),
                                    ls, ls);
  SID sid6 = Segment::AddNewSegment(TID(4), VTS::Parse("[0:3; 1:2; 2:2; 3:2;]"),
                                    ls, ls);


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
  CHECK_EQ(sid6.raw(), 6);
  CHECK_EQ(ssid6.raw(), 6);
}

// -------- Shadow Value ------------ {{{1
class ShadowValue {
 public:
  ShadowValue() {
    if (DEBUG_MODE) {
      rd_ssid_ = 0xDEADBEEF;
      wr_ssid_ = 0xDEADBEEF;
    }
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
  bool operator <  (const ShadowValue &sval) const {
    if (rd_ssid_ < sval.rd_ssid_) return true;
    if (rd_ssid_ == sval.rd_ssid_ && wr_ssid_ < sval.wr_ssid_) return true;
    return false;
  }

  string ToString() const {
    char buff[1000];
    if (IsNew()) {
      return "{New}";
    }
    snprintf(buff, sizeof(buff), "Reads: %s; Writes: %s",
            SegmentSet::ToStringWithLocks(rd_ssid()).c_str(),
            SegmentSet::ToStringWithLocks(wr_ssid()).c_str());
    return buff;
  }

 private:
  int32_t rd_ssid_;
  int32_t wr_ssid_;
};

// -------- CacheLine --------------- {{{1
class CacheLineBase {
 public:
  static const uintptr_t kLineSizeBits = Mask::kNBitsLog;  // Don't change this.
  static const uintptr_t kLineSize = Mask::kNBits;
 protected:
  uintptr_t tag_;

  Mask has_shadow_value_;
  Mask traced_;
  Mask racey_;
  Mask published_;
};

// Uncompressed line. Just a vector of kLineSize shadow values.
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

  const Mask &has_shadow_value() const { return has_shadow_value_;  }
  Mask &traced() { return traced_; }
  Mask &published() { return published_; }
  Mask &racey()  { return racey_; }
  uintptr_t tag() { return tag_; }

  // Add a new shadow value to a place where there was no shadow value before.
  ShadowValue *AddNewSvalAtOffset(uintptr_t off) {
    CHECK(!has_shadow_value().Get(off));
    has_shadow_value_.Set(off);
    published_.Clear(off);
    ShadowValue *res = GetValuePointer(off);
    res->Clear();
    return res;
  }

  // Return true if this line has no useful information in it.
  bool Empty() {
    // The line has shadow values.
    if (!has_shadow_value().Empty()) return false;
    // If the line is traced or published, we want to keep it.
    if (!traced().Empty()) return false;
    if (!published().Empty()) return false;
    // No shadow values, but has creator tid.
    return true;
  }

  INLINE Mask ClearRangeAndReturnOldUsed(uintptr_t from, uintptr_t to) {
    traced_.ClearRange(from, to);
    published_.ClearRange(from, to);
    racey_.ClearRange(from, to);
    return has_shadow_value_.ClearRangeAndReturnOld(from, to);
  }

  void Clear() {
    has_shadow_value_.Clear();
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
    if (off & (size - 1)) return false;  // Not aligned.
    DCHECK(off + size <= kLineSize);
    DCHECK(size == 2 || size == 4 || size == 8);
    if (has_shadow_value_.GetRange(off + 1, off + size) == 0)
      return true;
    return false;
  }

  static void InitClassMembers() {
    if (DEBUG_MODE) {
      Printf("sizeof(CacheLine) = %ld\n", sizeof(CacheLine));
    }
    free_list_ = new FreeList(sizeof(CacheLine), 1024);
  }

 private:
  explicit CacheLine(uintptr_t tag) {
    tag_ = tag;
    Clear();
  }
  ~CacheLine() { }

  // no non-static data members.

  static FreeList *free_list_;
};

FreeList *CacheLine::free_list_;

// If range [a,b) fits into one line, return that line's tag.
// Else range [a,b) is broken into these ranges:
//   [a, line1_tag)
//   [line1_tag, line2_tag)
//   [line2_tag, b)
// and 0 is returned.
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

// -------- Cache ------------------ {{{1
class Cache {
 public:
  Cache() {
    ForgetAllState();
  }

  // HOT
  INLINE CacheLine *GetLine(uintptr_t a, bool create_new_if_need, int call_site) {
    uintptr_t tag = CacheLine::ComputeTag(a);
    DCHECK(tag <= a);
    DCHECK(tag + CacheLine::kLineSize > a);
    uintptr_t cli = ComputeCacheLineIndexInCache(a);
    CacheLine *res = lines_[cli];
    if (LIKELY(res && res->tag() == tag)) {
      // G_stats->cache_fast_get++;
    } else {
      res = WriteBackAndFetch(tag, cli, create_new_if_need);
    }
    return res;
  }

  INLINE CacheLine *GetLineOrCreateNew(uintptr_t a, int call_site) {
    return GetLine(a, true, call_site);
  }
  INLINE CacheLine *GetLineIfExists(uintptr_t a, int call_site) {
    return GetLine(a, false, call_site);
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
    set<ShadowValue> all_svals;
    map<size_t, int> sizes;
    for (Map::iterator it = storage_.begin(); it != storage_.end(); ++it) {
      CacheLine *line = it->second;
      // uintptr_t cli = ComputeCacheLineIndexInCache(line->tag());
      //if (lines_[cli] == line) {
        // this line is in cache -- ignore it.
      //  continue;
      //}
      set<ShadowValue> s;
      for (uintptr_t i = 0; i < CacheLine::kLineSize; i++) {
        if (line->has_shadow_value().Get(i)) {
          ShadowValue sval = *(line->GetValuePointer(i));
          s.insert(sval);
          all_svals.insert(sval);
        }
      }
      size_t size = s.size();
      if (size > 10) size = 10;
      sizes[size]++;
    }
    Printf("Storage sizes: %ld\n", storage_.size());
    for (size_t size = 0; size <= CacheLine::kLineSize; size++) {
      if (sizes[size]) {
        Printf("  %ld => %d\n", size, sizes[size]);
      }
    }
    Printf("Different svals: %ld\n", all_svals.size());
    set <SSID> all_ssids;
    for (set<ShadowValue>::iterator it = all_svals.begin(); it != all_svals.end(); ++it) {
      ShadowValue sval = *it;
      for (int i = 0; i < 2; i++) {
        SSID ssid = i ? sval.rd_ssid() : sval.wr_ssid();
        all_ssids.insert(ssid);
      }
    }
    Printf("Different ssids: %ld\n", all_ssids.size());
    set <SID> all_sids;
    for (set<SSID>::iterator it = all_ssids.begin(); it != all_ssids.end(); ++it) {
      int size = SegmentSet::Size(*it);
      for (int i = 0; i < size; i++) {
        SID sid = SegmentSet::GetSID(*it, i, __LINE__);
        all_sids.insert(sid);
      }
    }
    Printf("Different sids: %ld\n", all_sids.size());
    for (int i = 1; i < Segment::NumberOfSegments(); i++) {
      if (Segment::ProfileSeg(SID(i)) && all_sids.count(SID(i)) == 0) {
        // Printf("Segment SID %d: missing in storage; ref=%d\n", i,
        // Segment::Get(SID(i))->ref_count());
      }
    }
  }

 private:
  INLINE uintptr_t ComputeCacheLineIndexInCache(uintptr_t addr) {
    return (addr >> CacheLine::kLineSizeBits) & (kNumLines - 1);
  }

  NOINLINE CacheLine *WriteBackAndFetch(uintptr_t tag, uintptr_t cli,
                                        bool create_new_if_need) {
    ScopedMallocCostCenter cc("Cache::WriteBackAndFetch");
    CacheLine *res;
    size_t old_storage_size = storage_.size();
    CacheLine **line_for_this_tag = NULL;
    if (create_new_if_need) {
      line_for_this_tag = &storage_[tag];
    } else {
      Map::iterator it = storage_.find(tag);
      if (it == storage_.end()) {
        return NULL;
      }
      line_for_this_tag = &(it->second);
    }
    CHECK(line_for_this_tag);
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
      DCHECK(!res->Empty());
      lines_[cli]        = res;
      G_stats->cache_fetch++;
    }


    if (old_line) {
      if (old_line->Empty()) {
        storage_.erase(old_line->tag());
        CacheLine::Delete(old_line);
        G_stats->cache_delete_empty_line++;
      } else {
        if (debug_cache) {
          DebugOnlyCheckCacheLineWhichWeReplace(old_line, res);
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

  void DebugOnlyCheckCacheLineWhichWeReplace(CacheLine *old_line,
                                             CacheLine *new_line) {
    static int c = 0;
    c++;
    if ((c % 1024) == 1) {
      set<int64_t> s;
      for (uintptr_t i = 0; i < CacheLine::kLineSize; i++) {
        if (old_line->has_shadow_value().Get(i)) {
          int64_t sval = *reinterpret_cast<int64_t*>(
                            old_line->GetValuePointer(i));
          // Printf("%p ", sval);
          s.insert(sval);
        }
      }
      Printf("\n[%d] Cache Size=%ld %s different values: %ld\n", c,
             storage_.size(), old_line->has_shadow_value().ToString().c_str(),
             s.size());

      Printf("new line: %p %p\n", new_line->tag(), new_line->tag()
             + CacheLine::kLineSize);
      G_stats->PrintStatsForCache();
    }
  }

  static const int kNumLines = 1 << 16;
  CacheLine *lines_[kNumLines];

  // tag => CacheLine
#if 1
  typedef hash_map<uintptr_t, CacheLine*> Map;
#else
  typedef map<uintptr_t, CacheLine*> Map;
#endif
  Map storage_;
};

static  Cache *G_cache;

// -------- Published range -------------------- {{{1
struct PublishInfo {
  uintptr_t tag;   // Tag of the cache line where the mem is published.
  Mask      mask;  // The bits that are actually published.
  VTS      *vts;   // The point where this range has been published.
};


typedef multimap<uintptr_t, PublishInfo> PublishInfoMap;

// Maps 'mem+size' to the PublishInfoMap{mem, size, vts}.
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

// Clear the publish attribute for the bytes from 'line' that are set in 'mask'
static void ClearPublishedAttribute(CacheLine *line, Mask mask) {
  CHECK(CheckSanityOfPublishedMemory(line->tag(), __LINE__));
  typedef PublishInfoMap::iterator Iter;
  bool deleted_some = true;
  if (kDebugPublish)
    Printf(" ClearPublishedAttribute: %p %s\n",
           line->tag(), mask.ToString().c_str());
  while (deleted_some) {
    deleted_some = false;
    pair<Iter, Iter> eq_range = g_publish_info_map->equal_range(line->tag());
    for (Iter it = eq_range.first; it != eq_range.second; ++it) {
      PublishInfo &info = it->second;
      DCHECK(info.tag == line->tag());
      if (kDebugPublish)
        Printf("?ClearPublishedAttribute: %p %s\n", line->tag(),
               info.mask.ToString().c_str());
      info.mask.Subtract(mask);
      if (kDebugPublish)
        Printf("+ClearPublishedAttribute: %p %s\n", line->tag(),
               info.mask.ToString().c_str());
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
  DCHECK(b <= CacheLine::kLineSize);
  DCHECK(a < b);
  uintptr_t tag = CacheLine::ComputeTag(addr);
  CHECK(CheckSanityOfPublishedMemory(tag, __LINE__));
  CacheLine *line = G_cache->GetLineOrCreateNew(tag, __LINE__);

  if (1 || line->published().GetRange(a, b)) {
    Mask mask(0);
    mask.SetRange(a, b);
    // TODO(timurrrr): add warning for re-publishing.
    ClearPublishedAttribute(line, mask);
  }

  line->published().SetRange(a, b);

  PublishInfo pub_info;
  pub_info.tag  = tag;
  pub_info.mask.SetRange(a, b);
  pub_info.vts  = vts->Clone();
  g_publish_info_map->insert(make_pair(tag, pub_info));
  G_stats->publish_set++;
  if (kDebugPublish)
    Printf("PublishRange   : [%p,%p) %p %s vts=%p\n",
           a, b, tag, pub_info.mask.ToString().c_str(), vts);
  CHECK(CheckSanityOfPublishedMemory(tag, __LINE__));
}

// Publish memory range [a, b).
static void PublishRange(uintptr_t a, uintptr_t b, VTS *vts) {
  CHECK(a);
  CHECK(a < b);
  if (kDebugPublish)
    Printf("PublishRange   : [%p,%p), size=%d, tag=%p\n",
           a, b, (int)(b - a), CacheLine::ComputeTag(a));
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
    PublishRangeInOneLine(tag_i, 0, CacheLine::kLineSize, vts);
  }
  if (b > line2_tag) {
    PublishRangeInOneLine(line2_tag, 0, b - line2_tag, vts);
  }
}

// -------- Clear Memory State ------------------ {{{1

static void NOINLINE UnrefSegmentsInMemoryRange(uintptr_t a, uintptr_t b,
                                                Mask mask, CacheLine *line) {
  DCHECK(!mask.Empty());
  for (uintptr_t x = a; x < b; x++) {  // slow?
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
                                      uintptr_t beg, uintptr_t end) {
  CacheLine *line = G_cache->GetLineIfExists(addr, __LINE__);
  // CacheLine *line = G_cache->GetLineOrCreateNew(addr, __LINE__);
  if (!line) return;
  DCHECK(beg < CacheLine::kLineSize);
  DCHECK(end <= CacheLine::kLineSize);
  DCHECK(beg < end);
  Mask published = line->published();
  if (UNLIKELY(!published.Empty())) {
    Mask mask(published.GetRange(beg, end));
    ClearPublishedAttribute(line, mask);
  }
  Mask old_used = line->ClearRangeAndReturnOldUsed(beg, end);
  if (UNLIKELY(!old_used.Empty())) {
    UnrefSegmentsInMemoryRange(beg, end, old_used, line);
  }
}



// clear memory state for [a,b)
void INLINE ClearMemoryState(uintptr_t a, uintptr_t b) {
  if (a == b) return;
  CHECK(a < b);
  uintptr_t line1_tag = 0, line2_tag = 0;
  uintptr_t single_line_tag = GetCacheLinesForRange(a, b,
                                                    &line1_tag, &line2_tag);
  if (single_line_tag) {
    ClearMemoryStateInOneLine(a, a - single_line_tag,
                              b - single_line_tag);
    return;
  }

  uintptr_t a_tag = CacheLine::ComputeTag(a);
  ClearMemoryStateInOneLine(a, a - a_tag, CacheLine::kLineSize);

  for (uintptr_t tag_i = line1_tag; tag_i < line2_tag;
       tag_i += CacheLine::kLineSize) {
    ClearMemoryStateInOneLine(tag_i, 0, CacheLine::kLineSize);
  }

  if (b > line2_tag) {
    ClearMemoryStateInOneLine(line2_tag, 0, b - line2_tag);
  }

  if (DEBUG_MODE && G_flags->debug_level >= 2) {
    // Check that we've cleared it. Slow!
    for (uintptr_t x = a; x < b; x++) {
      uintptr_t off = CacheLine::ComputeOffset(x);
      CacheLine *line = G_cache->GetLineOrCreateNew(x, __LINE__);
      CHECK(!line->has_shadow_value().Get(off));
    }
  }
}



// -------- ThreadSanitizerReport -------------- {{{1
struct ThreadSanitizerReport {
  // Types of reports.
  enum ReportType {
    DATA_RACE,
    UNLOCK_FOREIGN,
    UNLOCK_NONLOCKED,
    INVALID_LOCK
  };

  // Common fields.
  ReportType  type;
  TID         tid;
  StackTrace *stack_trace;

  const char *ReportName() const {
    switch (type) {
      case DATA_RACE:        return "Race";
      case UNLOCK_FOREIGN:   return "UnlockForeign";
      case UNLOCK_NONLOCKED: return "UnlockNonLocked";
      case INVALID_LOCK:     return "InvalidLock";
    }
    CHECK(0);
    return NULL;
  }

  virtual ~ThreadSanitizerReport() {
    StackTrace::Delete(stack_trace);
  }
};

static bool ThreadSanitizerPrintReport(ThreadSanitizerReport *report);

// DATA_RACE.
struct ThreadSanitizerDataRaceReport : public ThreadSanitizerReport {
  uintptr_t   racey_addr;
  string      racey_addr_description;
  uintptr_t   last_access_size;
  TID         last_access_tid;
  SID         last_access_sid;
  bool        last_access_is_w;
  LSID        last_acces_lsid[2];

  ShadowValue new_sval;
  ShadowValue old_sval;

  bool        is_expected;
  bool        racey_addr_was_published;
};

// Report for bad unlock (UNLOCK_FOREIGN, UNLOCK_NONLOCKED).
struct ThreadSanitizerBadUnlockReport : public ThreadSanitizerReport {
  LID lid;
};

// Report for invalid lock addresses (INVALID_LOCK).
struct ThreadSanitizerInvalidLockReport : public ThreadSanitizerReport {
  uintptr_t lock_addr;
};

// -------- LockHistory ------------- {{{1
// For each thread we store a limited amount of history of locks and unlocks.
// If there is a race report (in hybrid mode) we try to guess a lock
// which might have been used to pass the ownership of the object between
// threads.
//
// Thread1:                    Thread2:
// obj->UpdateMe();
// mu.Lock();
// flag = true;
// mu.Unlock(); // (*)
//                             mu.Lock();  // (**)
//                             bool f = flag;
//                             mu.Unlock();
//                             if (f)
//                                obj->UpdateMeAgain();
//
// For this code a hybrid detector may report a false race.
// LockHistory will find the lock mu and report it.

struct LockHistory {
 public:
  // LockHistory which will track no more than `size` recent locks
  // and the same amount of unlocks.
  LockHistory(size_t size): size_(size) { }

  // Record a Lock event.
  void OnLock(LID lid) {
    g_lock_era++;
    Push(LockHistoryElement(lid, g_lock_era), &locks_);
  }

  // Record an Unlock event.
  void OnUnlock(LID lid) {
    g_lock_era++;
    Push(LockHistoryElement(lid, g_lock_era), &unlocks_);
  }

  // Find locks such that:
  // - A Lock happend in `l`.
  // - An Unlock happened in `u`.
  // - Lock's era is greater than Unlock's era.
  // - Both eras are greater or equal than min_lock_era.
  static bool Intersect(const LockHistory &l, const LockHistory &u,
                        int32_t min_lock_era, set<LID> *locks) {
    const Queue &lq = l.locks_;
    const Queue &uq = u.unlocks_;
    for (size_t i = 0; i < lq.size(); i++) {
      int32_t l_era = lq[i].lock_era;
      if (l_era < min_lock_era) continue;
      LID lid = lq[i].lid;
      // We don't want to report pure happens-before locks since
      // they already create h-b arcs.
      if (Lock::LIDtoLock(lid)->is_pure_happens_before()) continue;
      for (size_t j = 0; j < uq.size(); j++) {
        int32_t u_era = uq[j].lock_era;
        if (lid != uq[j].lid) continue;
        // Report("LockHistory::Intersect: L%d %d %d %d\n", lid.raw(), min_lock_era, u_era, l_era);
        if (u_era < min_lock_era)  continue;
        if (u_era > l_era) continue;
        locks->insert(lid);
      }
    }
    return !locks->empty();
  }

  void PrintLocks() const { Print(&locks_); }
  void PrintUnlocks() const { Print(&unlocks_); }

 private:
  struct LockHistoryElement {
    LID lid;
    uint32_t lock_era;
    LockHistoryElement(LID l, uint32_t era)
        : lid(l),
        lock_era(era) {
        }
  };

  typedef deque<LockHistoryElement> Queue;

  void Push(LockHistoryElement e, Queue *q) {
    CHECK(q->size() <= size_);
    if (q->size() == size_)
      q->pop_front();
    q->push_back(e);
  }

  void Print(const Queue *q) const {
    set<LID> printed;
    for (size_t i = 0; i < q->size(); i++) {
      const LockHistoryElement &e = (*q)[i];
      if (printed.count(e.lid)) continue;
      Report("era %d: \n", e.lock_era);
      Lock::ReportLockWithOrWithoutContext(e.lid, true);
      printed.insert(e.lid);
    }
  }

  Queue locks_;
  Queue unlocks_;
  size_t size_;
};

// -------- RecentSegmentsCache ------------- {{{1
// For each thread we store a limited amount of recent segments with
// the same VTS and LS as the current segment.
// When a thread enters a new basic block, we can sometimes reuse a
// recent segment if it is the same or not used anymore (see Search()).
//
// We need to flush the cache when current lockset changes or the current
// VTS changes or we do ForgetAllState.
// TODO(timurrrr): probably we can cache segments with different LSes and
// compare their LS with the current LS.
struct RecentSegmentsCache {
 public:
  RecentSegmentsCache(int cache_size) : cache_size_(cache_size) {}
  ~RecentSegmentsCache() { Clear(); }

  void Clear() {
    ShortenQueue(0);
  }

  void Push(SID sid) {
    queue_.push_front(sid);
    Segment::Ref(sid, "RecentSegmentsCache::ShortenQueue");
    ShortenQueue(cache_size_);
  }

  void ForgetAllState() {
    queue_.clear();  // Don't unref - the segments are already dead.
  }

  INLINE SID Search(const vector<uintptr_t> &curr_stack,
                    SID curr_sid, /*OUT*/ bool *needs_refill) {
    // TODO(timurrrr): we can probably move the matched segment to the head
    // of the queue.

    deque<SID>::iterator it = queue_.begin();
    for (; it != queue_.end(); it++) {
      SID sid = *it;
      Segment *seg = Segment::Get(sid);

      if (seg->ref_count() == 1 + (sid == curr_sid)) {
        // The current segment is not used anywhere else,
        // so just replace the stack trace in it.
        // The refcount of an unused segment is equal to
        // *) 1 if it is stored only in the cache,
        // *) 2 if it is the current segment of the Thread.
        G_stats->history_reuses_segment++;
        *needs_refill = true;
        return sid;
      }

      // Check three top entries of the call stack of the recent segment.
      // If they match the current segment stack, don't create a new segment.
      // This can probably lead to a little bit wrong stack traces in rare
      // occasions but we don't really care that much.
      if (kSizeOfHistoryStackTrace > 0) {
        size_t n = curr_stack.size();
        uintptr_t *emb_trace = Segment::embedded_stack_trace(sid);
        if(*emb_trace &&  // This stack trace was filled
           curr_stack.size() >= 3 &&
           emb_trace[0] == curr_stack[n-1] &&
           emb_trace[1] == curr_stack[n-2] &&
           emb_trace[2] == curr_stack[n-3]) {
          G_stats->history_uses_same_segment++;
          *needs_refill = false;
          return sid;
        }
      }
    }

    return SID();
  }

 private:
  void ShortenQueue(size_t flush_to_length) {
    while(queue_.size() > flush_to_length) {
      SID sid = queue_.back();
      Segment::Unref(sid, "RecentSegmentsCache::ShortenQueue");
      queue_.pop_back();
    }
  }

  deque<SID> queue_;
  size_t cache_size_;
};

// -------- TraceInfo ------------------ {{{1
size_t TraceInfo::id_counter_;
vector<TraceInfo*> *TraceInfo::g_all_traces;

TraceInfo *TraceInfo::NewTraceInfo(size_t n_mops, uintptr_t pc) {
  size_t mem_size = (sizeof(TraceInfo) + (n_mops - 1) * sizeof(MopInfo));
  uint8_t *mem = new uint8_t[mem_size];
  memset(mem, 0xab, mem_size);
  TraceInfo *res = new (mem) TraceInfo;
  res->n_mops_ = n_mops;
  res->pc_ = pc;
  res->id_ = id_counter_++;
  res->counter_ = 0;
  res->generate_segments_ =
      ThreadSanitizerWantToCreateSegmentsOnSblockEntry(pc);
  if (g_all_traces == NULL) {
    g_all_traces = new vector<TraceInfo*>;
    CHECK(id_counter_ == 1);
  }
  g_all_traces->push_back(res);
  return res;
}

void TraceInfo::PrintTraceProfile() {
  int64_t total_counter = 0;
  multimap<size_t, TraceInfo*> traces;
  for (size_t i = 0; i < g_all_traces->size(); i++) {
    TraceInfo *trace = (*g_all_traces)[i];
    traces.insert(make_pair(trace->counter(), trace));
    total_counter += trace->counter();
  }
  Printf("TraceProfile: %ld traces, %lld hits\n",
         g_all_traces->size(), total_counter);
  int i = 0;
  for (multimap<size_t, TraceInfo*>::reverse_iterator it = traces.rbegin();
       it != traces.rend(); ++it, i++) {
    TraceInfo *trace = it->second;
    int64_t c = it->first;
    int64_t permile = (c * 1000) / total_counter;
    if (permile == 0 || i >= 20) break;
    Printf("TR%ld c=%lld (%lld/1000) n_mops=%ld\n", trace->id(), c,
           permile, trace->n_mops());
  }
}

// -------- Thread ------------------ {{{1
struct Thread {
 public:
  Thread(TID tid, TID parent_tid, VTS *vts, StackTrace *creation_context)
    : is_running_(true),
      tid_(tid),
      sid_(0),
      parent_tid_(parent_tid),
      max_sp_(0),
      min_sp_(0),
      creation_context_(creation_context),
      announced_(false),
      rd_lockset_(0),
      wr_lockset_(0),
      vts_at_exit_(NULL),
      lock_history_(128),
      recent_segments_cache_(G_flags->recent_segments_cache_size) {

    NewSegmentWithoutUnrefingOld("Thread Creation", vts);

    call_stack_.reserve(100);
    call_stack_ignore_rec_.reserve(100);
    HandleRtnCall(0, 0, IGNORE_BELOW_RTN_UNKNOWN);
    ignore_[0] = ignore_[1] = 0;
    ignore_context_[0] = ignore_context_[1] = NULL;
    if (tid != TID(0) && parent_tid.valid()) {
      CHECK(creation_context_);
    }

    // Add myself to the array of threads.
    CHECK(tid.raw() < G_flags->max_n_threads);
    CHECK(all_threads_[tid.raw()] == NULL);
    n_threads_ = max(n_threads_, tid.raw() + 1);
    all_threads_[tid.raw()] = this;
  }

  TID tid() const { return tid_; }
  TID parent_tid() const { return parent_tid_; }

  // STACK
  uintptr_t max_sp() const { return max_sp_; }
  uintptr_t min_sp() const { return min_sp_; }

  void SetStack(uintptr_t stack_min, uintptr_t stack_max) {
    CHECK(stack_min < stack_max);
    // Stay sane. Expect stack less than 64M.
    CHECK(stack_max - stack_min <= 64 * 1024 * 1024);
    min_sp_ = stack_min;
    max_sp_ = stack_max;
  }

  bool MemoryIsInStack(uintptr_t a) {
    return a >= min_sp_ && a <= max_sp_;
  }


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
    snprintf(buff, sizeof(buff), "T%d", tid().raw());
    string res = buff;
    if (thread_name_.length() > 0) {
      res += " (";
      res += thread_name_;
      res += ")";
    }
    return res;
  }

  bool is_running() const { return is_running_; }

  // ignore
  INLINE void set_ignore(bool is_w, bool on) {
    ignore_[is_w] += on ? 1 : -1;
    CHECK(ignore_[is_w] >= 0);
    if (DEBUG_MODE && on && G_flags->debug_level >= 1) {
      StackTrace::Delete(ignore_context_[is_w]);
      ignore_context_[is_w] = CreateStackTrace(0, 3);
    }
  }
  INLINE void set_ignore_all(bool on) {
    set_ignore(false, on);
    set_ignore(true, on);
  }
  bool ignore(bool is_w) { return ignore_[is_w] > 0; }
  bool ignore_all() { return (ignore_[0] > 0) && (ignore_[1] > 0); }

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

  void set_thread_name(const char *name) {
    thread_name_ = string(name);
  }

  void HandleThreadEnd() {
    CHECK(is_running_);
    is_running_ = false;
    CHECK(!vts_at_exit_);
    vts_at_exit_ = vts()->Clone();
    CHECK(vts_at_exit_);
  }

  // Return the TID of the joined child and it's vts
  TID HandleThreadJoinAfter(VTS **vts_at_exit, TID joined_tid) {
    CHECK(joined_tid.raw() > 0);
    *vts_at_exit = Thread::Get(joined_tid)->vts_at_exit_;
    if (*vts_at_exit == NULL) {
      Printf("vts_at_exit==NULL; parent=%d, child=%d\n",
             tid().raw(), joined_tid.raw());
    }
    CHECK(*vts_at_exit);
    if (0)
    Printf("T%d: vts_at_exit_: %s\n", joined_tid.raw(),
           (*vts_at_exit)->ToString().c_str());
    return joined_tid;
  }

  static int NumberOfThreads() { return n_threads_; }

  static Thread *GetIfExists(TID tid) {
    if (tid.raw() < NumberOfThreads())
      return Get(tid);
    return NULL;
  }

  static Thread *Get(TID tid) {
    DCHECK(tid.raw() < NumberOfThreads());
    return all_threads_[tid.raw()];
  }

  // Locks
  void HandleLock(uintptr_t lock_addr, bool is_w_lock) {

    if (debug_lock) {
      Printf("T%d %sLock   %p; %s\n",
           tid_.raw(),
           is_w_lock ? "Wr" : "Rd",
           lock_addr,
           LockSet::ToString(lsid(is_w_lock)).c_str());

      ReportStackTrace(0, 7);
    }

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
      HandleWait(lock->lock_addr());
    }
    if (G_flags->suggest_happens_before_arcs) {
      lock_history_.OnLock(lock->lid());
    }
    NewSegmentForLockingEvent();
  }

  void HandleUnlock(uintptr_t lock_addr) {
    Lock *lock = Lock::Lookup(lock_addr);
    // If the lock is not found, report an error.
    if (lock == NULL) {
      ThreadSanitizerInvalidLockReport *report =
          new ThreadSanitizerInvalidLockReport;
      report->type = ThreadSanitizerReport::INVALID_LOCK;
      report->tid = tid();
      report->lock_addr = lock_addr;
      report->stack_trace = CreateStackTrace();
      ThreadSanitizerPrintReport(report);
      return;
    }
    bool is_w_lock = lock->wr_held();

    if (debug_lock) {
      Printf("T%d %sUnlock %p; %s\n",
             tid_.raw(),
             is_w_lock ? "Wr" : "Rd",
             lock_addr,
             LockSet::ToString(lsid(is_w_lock)).c_str());
      ReportStackTrace(0, 7);
    }

    if (G_flags->pure_happens_before || lock->is_pure_happens_before()) {
      HandleSignal(lock->lock_addr());
    }

    if (!lock->wr_held() && !lock->rd_held()) {
      ThreadSanitizerBadUnlockReport *report =
          new ThreadSanitizerBadUnlockReport;
      report->type = ThreadSanitizerReport::UNLOCK_NONLOCKED;
      report->tid = tid();
      report->lid = lock->lid();
      report->stack_trace = CreateStackTrace();
      ThreadSanitizerPrintReport(report);
      return;
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
      ThreadSanitizerBadUnlockReport *report =
          new ThreadSanitizerBadUnlockReport;
      report->type = ThreadSanitizerReport::UNLOCK_FOREIGN;
      report->tid = tid();
      report->lid = lock->lid();
      report->stack_trace = CreateStackTrace();
      ThreadSanitizerPrintReport(report);
    }

    if (G_flags->suggest_happens_before_arcs) {
      lock_history_.OnUnlock(lock->lid());
    }

    NewSegmentForLockingEvent();

  }

  LSID lsid(bool is_w) {
    return is_w ? wr_lockset_ : rd_lockset_;
  }

  const LockHistory &lock_history() { return lock_history_; }

  // SIGNAL/WAIT events.
  void HandleWait(uintptr_t cv) {

    SignallerMap::iterator it = signaller_map_->find(cv);
    if (it != signaller_map_->end()) {
      const VTS *signaller_vts = it->second.vts;
      NewSegmentForWait(signaller_vts);
    }

    if (debug_happens_before) {
      Printf("T%d: Wait: %p:\n    %s %s\n", tid_.raw(),
             cv,
             vts()->ToString().c_str(),
             Segment::ToString(sid()).c_str());
      if (G_flags->debug_level >= 1) {
        ReportStackTrace();
      }
    }
  }


  void HandleSignal(uintptr_t cv) {
    Signaller *signaller = &(*signaller_map_)[cv];
    if (!signaller->vts) {
      signaller->vts = vts()->Clone();
    } else {
      VTS *new_vts = VTS::Join(signaller->vts, vts());
      VTS::Delete(signaller->vts);
      signaller->vts = new_vts;
    }
    NewSegmentForSignal();
    if (debug_happens_before) {
      Printf("T%d: Signal: %p:\n    %s %s\n    %s\n", tid_.raw(), cv,
             vts()->ToString().c_str(), Segment::ToString(sid()).c_str(),
             (signaller->vts)->ToString().c_str());
      if (G_flags->debug_level >= 1) {
        ReportStackTrace();
      }
    }
  }

  void INLINE NewSegmentWithoutUnrefingOld(const char *call_site,
                                           VTS *new_vts) {
    DCHECK(new_vts);
    SID new_sid = Segment::AddNewSegment(tid(), new_vts,
                                         rd_lockset_, wr_lockset_);
    SID old_sid = sid();
    if (old_sid.raw() != 0 && new_vts != vts()) {
      // Flush the cache if VTS changed - the VTS won't repeat.
      recent_segments_cache_.Clear();
    }
    sid_ = new_sid;
    Segment::Ref(new_sid, "Thread::NewSegmentWithoutUnrefingOld");

    if (kSizeOfHistoryStackTrace > 0) {
      FillEmbeddedStackTrace(Segment::embedded_stack_trace(sid()));
    }
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
    // Flush the cache since we can't reuse segments with different lockset.
    recent_segments_cache_.Clear();
    NewSegment(__FUNCTION__, vts()->Clone());
  }

  void NewSegmentForMallocEvent() {
    // Flush the cache since we can't reuse segments with different lockset.
    recent_segments_cache_.Clear();
    NewSegment(__FUNCTION__, vts()->Clone());
  }


  void SetTopPc(uintptr_t pc) {
    DCHECK(!call_stack_.empty());
    if (pc) {
      call_stack_.back() = pc;
    }
  }

  void INLINE HandleSblockEnter(uintptr_t pc) {
    if (!G_flags->keep_history) return;
    if (g_so_far_only_one_thread) return;

    SetTopPc(pc);

    bool refill_stack = false;
    SID match = recent_segments_cache_.Search(call_stack_, sid(),
                                              /*OUT*/&refill_stack);
    if (match.valid()) {
      if (sid_ != match) {
        Segment::Ref(match, "Thread::HandleSblockEnter");
        Segment::Unref(sid_, "Thread::HandleSblockEnter");
        sid_ = match;
      }
      if (refill_stack && kSizeOfHistoryStackTrace > 0) {
        FillEmbeddedStackTrace(Segment::embedded_stack_trace(sid()));
      }
    } else {
      G_stats->history_creates_new_segment++;
      VTS *new_vts = vts()->Clone();
      NewSegment("HandleSblockEnter", new_vts);
      recent_segments_cache_.Push(sid());
    }
  }

  void NewSegmentForWait(const VTS *signaller_vts) {
    const VTS *current_vts   = vts();
    if (0)
    Printf("T%d NewSegmentForWait: \n  %s\n  %s\n", tid().raw(),
           current_vts->ToString().c_str(),
           signaller_vts->ToString().c_str());
    // We don't want to create a happens-before arc if it will be redundant.
    if (!VTS::HappensBeforeCached(signaller_vts, current_vts)) {
      VTS *new_vts = VTS::Join(current_vts, signaller_vts);
      NewSegment("NewSegmentForWait", new_vts);
    }
    DCHECK(VTS::HappensBeforeCached(signaller_vts, vts()));
  }

  void NewSegmentForSignal() {
    VTS *cur_vts = vts();
    VTS *new_vts = VTS::CopyAndTick(cur_vts, tid());
    NewSegment("NewSegmentForSignal", new_vts);
  }

  // When creating a child thread, we need to know
  // 1. where the thread was created (ctx)
  // 2. What was the vector clock of the parent thread (vts).

  struct ThreadCreateInfo {
    StackTrace *ctx;
    VTS        *vts;
  };

  // This event comes before the child is created (e.g. just
  // as we entered pthread_create).
  void HandleThreadCreateBefore(TID parent_tid, uintptr_t pc) {
    CHECK(parent_tid == tid());
    g_so_far_only_one_thread = false;
    // Store ctx and vts under TID(0).
    ThreadCreateInfo info;
    info.ctx = CreateStackTrace(pc);
    info.vts = vts()->Clone();
    CHECK(info.ctx && info.vts);
    child_tid_to_create_info_[TID(0)] = info;
    // Tick vts.
    this->NewSegmentForSignal();

    if (debug_thread) {
      Printf("T%d: THR_CREATE_BEFORE\n", parent_tid.raw());
    }
  }

  // This event comes when we are exiting the thread creation routine.
  // It may appear before *or* after THR_START event, at least with PIN.
  void HandleThreadCreateAfter(TID parent_tid, TID child_tid) {
    CHECK(parent_tid == tid());
    // Place the info under child_tid if we did not use it yet.
    if (child_tid_to_create_info_.count(TID(0))){
      child_tid_to_create_info_[child_tid] = child_tid_to_create_info_[TID(0)];
      child_tid_to_create_info_.erase(TID(0));
    }

    if (debug_thread) {
      Printf("T%d: THR_CREATE_AFTER %d\n", parent_tid.raw(), child_tid.raw());
    }
  }

  void HandleChildThreadStart(TID child_tid, VTS **vts, StackTrace **ctx) {
    Thread *parent = this;
    ThreadCreateInfo info;
    if (child_tid_to_create_info_.count(child_tid)) {
      // We already seen THR_CREATE_AFTER, so the info is under child_tid.
      info = child_tid_to_create_info_[child_tid];
      child_tid_to_create_info_.erase(child_tid);
      CHECK(info.ctx && info.vts);
    } else if (child_tid_to_create_info_.count(TID(0))){
      // We have not seen THR_CREATE_AFTER, but already seen THR_CREATE_BEFORE.
      info = child_tid_to_create_info_[TID(0)];
      child_tid_to_create_info_.erase(TID(0));
      CHECK(info.ctx && info.vts);
    } else {
      // We have not seen THR_CREATE_BEFORE/THR_CREATE_AFTER.
      // If the tool is single-threaded (valgrind) these events are redundant.
      info.ctx = parent->CreateStackTrace();
      info.vts = parent->vts()->Clone();
      parent->NewSegmentForSignal();
    }
    *ctx = info.ctx;
    VTS *singleton = VTS::CreateSingleton(child_tid);
    *vts = VTS::Join(singleton, info.vts);
    VTS::Delete(singleton);
    VTS::Delete(info.vts);


    if (debug_thread) {
      Printf("T%d: THR_START parent: T%d : %s %s\n", child_tid.raw(),
             parent->tid().raw(),
             parent->vts()->ToString().c_str(),
             (*vts)->ToString().c_str());
      if (G_flags->announce_threads) {
        Printf("%s\n", (*ctx)->ToString().c_str());
      }
    }
  }

  // Support for Cyclic Barrier, e.g. pthread_barrier_t.
  // We need to create (barrier_count-1)^2 h-b arcs between
  // threads blocking on a barrier. We should not create any h-b arcs
  // for two calls to barrier_wait if the barrier was reset between then.
  struct CyclicBarrierInfo {
    // The value given to barrier_init.
    uint32_t barrier_count;
    // How many times we may block on this barrier before resetting.
    int32_t calls_before_reset;
    // How many times we entered the 'wait-before' and 'wait-after' handlers.
    int32_t n_wait_before, n_wait_after;
  };
  // The following situation is possible:
  // - N threads blocked on a barrier.
  // - All N threads reached the barrier and we started getting 'wait-after'
  //   events, but did not yet get all of them.
  // - N threads blocked on the barrier again and we started getting
  //   'wait-before' events from the next barrier epoch.
  // - We continue getting 'wait-after' events from the previous epoch.
  //
  // We don't want to create h-b arcs between barrier events of different
  // epochs, so we use 'barrier + (epoch % 4)' as an object on which we
  // signal and wait (it is unlikely that more than 4 epochs are live at once.
  enum { kNumberOfPossibleBarrierEpochsLiveAtOnce = 4 };
  // Maps the barrier pointer to CyclicBarrierInfo.
  typedef hash_map<uintptr_t, CyclicBarrierInfo> CyclicBarrierMap;

  CyclicBarrierInfo &GetCyclicBarrierInfo(uintptr_t barrier) {
    if (cyclic_barrier_map_ == NULL) {
      cyclic_barrier_map_ = new CyclicBarrierMap;
    }
    return (*cyclic_barrier_map_)[barrier];
  }

  void HandleBarrierInit(uintptr_t barrier, uint32_t n) {
    CyclicBarrierInfo &info = GetCyclicBarrierInfo(barrier);
    CHECK(n > 0);
    memset(&info, 0, sizeof(CyclicBarrierInfo));
    info.barrier_count = n;
  }

  void HandleBarrierWaitBefore(uintptr_t barrier) {
    CyclicBarrierInfo &info = GetCyclicBarrierInfo(barrier);

    CHECK(info.calls_before_reset >= 0);
    int32_t epoch = info.n_wait_before / info.barrier_count;
    epoch %= kNumberOfPossibleBarrierEpochsLiveAtOnce;
    info.n_wait_before++;
    if (info.calls_before_reset == 0) {
      // We are blocking the first time after reset. Clear the VTS.
      info.calls_before_reset = info.barrier_count;
      Signaller &signaller = (*signaller_map_)[barrier + epoch];
      VTS::Delete(signaller.vts);
      signaller.vts = NULL;
      if (debug_happens_before) {
        Printf("T%d barrier %p (epoch %d) reset\n", tid().raw(),
               barrier, epoch);
      }
    }
    info.calls_before_reset--;
    // Signal to all threads that blocked on this barrier.
    if (debug_happens_before) {
      Printf("T%d barrier %p (epoch %d) wait before\n", tid().raw(),
             barrier, epoch);
    }
    HandleSignal(barrier + epoch);
  }

  void HandleBarrierWaitAfter(uintptr_t barrier) {
    CyclicBarrierInfo &info = GetCyclicBarrierInfo(barrier);
    int32_t epoch = info.n_wait_after / info.barrier_count;
    epoch %= kNumberOfPossibleBarrierEpochsLiveAtOnce;
    info.n_wait_after++;
    if (debug_happens_before) {
      Printf("T%d barrier %p (epoch %d) wait after\n", tid().raw(),
             barrier, epoch);
    }
    HandleWait(barrier + epoch);
  }

  // Call stack  -------------
  void PopCallStack() {
    CHECK(!call_stack_.empty());
    call_stack_.pop_back();
  }

  void HandleRtnCall(uintptr_t call_pc, uintptr_t target_pc,
                     IGNORE_BELOW_RTN ignore_below) {
    G_stats->events[RTN_CALL]++;
    if (!call_stack_.empty() && call_pc) {
      call_stack_.back() = call_pc;
    }
    call_stack_.push_back(target_pc);

    bool ignore = false;
    if (ignore_below == IGNORE_BELOW_RTN_UNKNOWN) {
      if (ignore_below_cache_.Lookup(target_pc, &ignore) == false) {
        ignore = ThreadSanitizerIgnoreAccessesBelowFunction(target_pc);
        ignore_below_cache_.Insert(target_pc, ignore);
        G_stats->ignore_below_cache_miss++;
      } else {
        // Just in case, check the result of caching.
        DCHECK(ignore ==
               ThreadSanitizerIgnoreAccessesBelowFunction(target_pc));
      }
    } else {
      DCHECK(ignore_below == IGNORE_BELOW_RTN_YES ||
             ignore_below == IGNORE_BELOW_RTN_NO);
      ignore = ignore_below == IGNORE_BELOW_RTN_YES;
    }

    if (ignore) {
      set_ignore_all(true);
    }
    call_stack_ignore_rec_.push_back(ignore);
  }

  void HandleRtnExit() {
    G_stats->events[RTN_EXIT]++;
    if (!call_stack_.empty()) {
      if (call_stack_ignore_rec_.back())
        set_ignore_all(false);
      call_stack_ignore_rec_.pop_back();
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

  static void ForgetAllState() {
    // G_flags->debug_level = 2;
    for (int i = 0; i < Thread::NumberOfThreads(); i++) {
      Thread *thr = Get(TID(i));
      thr->recent_segments_cache_.ForgetAllState();
      thr->sid_ = SID();  // Reset the old SID so we don't try to read its VTS.
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
    ScopedMallocCostCenter malloc_cc("InitClassMembers");
    all_threads_        = new Thread*[G_flags->max_n_threads];
    memset(all_threads_, 0, sizeof(Thread*) * G_flags->max_n_threads);
    n_threads_          = 0;
    signaller_map_      = new SignallerMap;
  }

 private:
  bool is_running_;
  string thread_name_;

  TID    tid_;         // This thread's tid.
  SID    sid_;         // Current segment ID.
  TID    parent_tid_;  // Parent's tid.
  bool   thread_local_copy_of_g_has_expensive_flags_;
  uintptr_t  max_sp_;
  uintptr_t  min_sp_;
  StackTrace *creation_context_;
  bool      announced_;

  LSID   rd_lockset_;
  LSID   wr_lockset_;

  int ignore_[2];  // 0 for reads, 1 for writes.
  StackTrace *ignore_context_[2];

  VTS *vts_at_exit_;

  vector<uintptr_t> call_stack_;
  // Contains "true" for those functions in the stacktrace which inclusively
  // ignore memory accesses.
  vector<unsigned char> call_stack_ignore_rec_;

  PtrToBoolCache<251> ignore_below_cache_;

  LockHistory lock_history_;
  RecentSegmentsCache recent_segments_cache_;

  map<TID, ThreadCreateInfo> child_tid_to_create_info_;

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

  // All threads. The main thread has tid 0.
  static Thread **all_threads_;
  static int      n_threads_;

  // signaller address -> VTS
  static SignallerMap *signaller_map_;
  static CyclicBarrierMap *cyclic_barrier_map_;
};

// Thread:: static members
Thread                    **Thread::all_threads_;
int                         Thread::n_threads_;
Thread::SignallerMap       *Thread::signaller_map_;
Thread::CyclicBarrierMap   *Thread::cyclic_barrier_map_;


// -------- Unpublish memory range [a,b) {{{1
// Create happens-before arcs from all previous accesses to memory [a,b)
// to here.
static void UnpublishRange(uintptr_t a, uintptr_t b, Thread *thread) {
  CHECK(a < b);
  for (uintptr_t x = a; x < b; x++) {
    CacheLine *line = G_cache->GetLineOrCreateNew(x, __LINE__);
    CHECK(line);
    uintptr_t off = CacheLine::ComputeOffset(x);
    if (!line->has_shadow_value().Get(off)) continue;
    ShadowValue sval = line->GetValue(off);
    // Printf("UnpublishRange: %x %s\n", x, sval.ToString().c_str());
    SSID ssids[2];
    ssids[0] = sval.rd_ssid();
    ssids[1] = sval.wr_ssid();
    for (int i = 0; i < 2; i++) {
      SSID ssid = ssids[i];
      if (ssid.IsEmpty()) continue;
      for (int j = 0; j < SegmentSet::Size(ssid); j++) {
        SID sid = SegmentSet::GetSID(ssid, j, __LINE__);
        if (Segment::Get(sid)->tid() == thread->tid()) continue;
        thread->NewSegmentForWait(Segment::Get(sid)->vts());
      }
    }
  }
}

// -------- PCQ --------------------- {{{1
struct PCQ {
  uintptr_t pcq_addr;
  deque<VTS*> putters;
};

typedef map<uintptr_t, PCQ> PCQMap;
static PCQMap *g_pcq_map;

// -------- Heap info ---------------------- {{{1
#include "ts_heap_info.h"
// Information about heap memory.

struct HeapInfo {
  uintptr_t   ptr;
  uintptr_t   size;
  SID         sid;
  HeapInfo() :ptr(0), size(0), sid(0) { }

  Segment *seg() { return Segment::Get(sid); }
  TID tid() { return seg()->tid(); }
  string StackTraceString() { return Segment::StackTraceString(sid); }
};

static HeapMap<HeapInfo> *G_heap_map;

// -------- Forget all state -------- {{{1
// We need to forget all state and start over because we've
// run out of some resources (most likely, segment IDs).
static void ForgetAllStateAndStartOver(const char *reason) {
  g_last_flush_time = TimeInMilliSeconds();
  Report("INFO: %s. Flushing state.\n", reason);

  if (0) {
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

  G_heap_map->Clear();

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
// -------- Expected Race ---------------------- {{{1
typedef  HeapMap<ExpectedRace> ExpectedRacesMap;
static ExpectedRacesMap *G_expected_races_map;
static bool g_expecting_races;
static int g_found_races_since_EXPECT_RACE_BEGIN;

ExpectedRace* ThreadSanitizerFindExpectedRace(uintptr_t addr) {
  return G_expected_races_map->GetInfo(addr);
}

// -------- Suppressions ----------------------- {{{1
static const char default_suppressions[] =
// TODO(kcc): as it gets bigger, move it into a separate object file.
"# We need to have some default suppressions, but we don't want to    \n"
"# keep them in a separate text file, so we keep the in the code.     \n"

#ifdef VGO_darwin
"{                                                                    \n"
"   dyld tries to unlock an invalid mutex when adding/removing image. \n"
"   ThreadSanitizer:InvalidLock                                       \n"
"   fun:pthread_mutex_unlock                                          \n"
"   fun:_dyld_register_func_for_*_image                               \n"
"}                                                                    \n"
#endif

#ifdef _MSC_VER
"{                                                                   \n"
"  False lock report inside ntdll.dll                                \n"
"  ThreadSanitizer:InvalidLock                                       \n"
"  fun:*                                                             \n"
"  obj:*ntdll.dll                                                    \n"
"}                                                                   \n"
"{                                                                   \n"
"  False report due to lack of debug symbols in ntdll.dll  (a)       \n"
"  ThreadSanitizer:InvalidLock                                       \n"
"  fun:*SRWLock*                                                     \n"
"}                                                                   \n"
"{                                                                   \n"
"  False report due to lack of debug symbols in ntdll.dll  (b)       \n"
"  ThreadSanitizer:UnlockForeign                                     \n"
"  fun:*SRWLock*                                                     \n"
"}                                                                   \n"
"{                                                                   \n"
"  False report due to lack of debug symbols in ntdll.dll  (c)       \n"
"  ThreadSanitizer:UnlockNonLocked                                   \n"
"  fun:*SRWLock*                                                     \n"
"}                                                                   \n"

#endif

;

// -------- Report Storage --------------------- {{{1
class ReportStorage {
 public:

  ReportStorage()
   : n_reports(0),
     n_race_reports(0),
     program_finished_(0) {
    if (G_flags->generate_suppressions) {
      Report("INFO: generate_suppressions = true\n");
    }
    // Read default suppressions
    suppressions_.ReadFromString(default_suppressions);

    // Read user-supplied suppressions.
    for (size_t i = 0; i < G_flags->suppressions.size(); i++) {
      const string &supp_path = G_flags->suppressions[i];
      Report("INFO: reading suppressions file %s\n", supp_path.c_str());
      int n = suppressions_.ReadFromString(ReadFileToString(supp_path, true));
      Report("INFO: %6d suppression(s) read from file %s\n",
             n, supp_path.c_str());
    }
  }

  bool NOINLINE AddReport(TID tid, uintptr_t pc, bool is_w, uintptr_t addr,
                          int size,
                          ShadowValue old_sval, ShadowValue new_sval,
                          bool is_published) {
    Thread *thr = Thread::Get(tid);
    {
      // Check this isn't a "_ZNSs4_Rep20_S_empty_rep_storageE" report.
      uintptr_t offset;
      string symbol_descr;
      if (GetNameAndOffsetOfGlobalObject(addr, &symbol_descr, &offset)) {
        if (StringMatch("*empty_rep_storage*", symbol_descr))
          return false;
      }
    }

    bool is_expected = false;
    ExpectedRace *expected_race = G_expected_races_map->GetInfo(addr);
    if (expected_race) {
      is_expected = true;
      expected_race->count++;
    }

    if (g_expecting_races) {
      is_expected = true;
      g_found_races_since_EXPECT_RACE_BEGIN++;
    }

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
    race_report->stack_trace = stack_trace;
    race_report->racey_addr_was_published = is_published;
    race_report->last_acces_lsid[false] = thr->lsid(false);
    race_report->last_acces_lsid[true] = thr->lsid(true);

    Segment *seg = Segment::Get(thr->sid());
    CHECK(thr->lsid(false) == seg->lsid(false));
    CHECK(thr->lsid(true) == seg->lsid(true));

    return ThreadSanitizerPrintReport(race_report);
  }

  void AnnounceThreadsInSegmentSet(SSID ssid) {
    if (ssid.IsEmpty()) return;
    for (int s = 0; s < SegmentSet::Size(ssid); s++) {
      Segment *seg = SegmentSet::GetSegmentForNonSingleton(ssid, s, __LINE__);
      Thread::Get(seg->tid())->Announce();
    }
  }



  void PrintConcurrentSegmentSet(SSID ssid, TID tid, SID sid,
                                 LSID lsid, bool is_w,
                                 const char *descr, set<LID> *locks,
                                 set<SID>* concurrent_sids) {
    if (ssid.IsEmpty()) return;
    bool printed_header = false;
    Thread *thr1 = Thread::Get(tid);
    for (int s = 0; s < SegmentSet::Size(ssid); s++) {
      SID concurrent_sid = SegmentSet::GetSID(ssid, s, __LINE__);
      Segment *seg = Segment::Get(concurrent_sid);
      if (Segment::HappensBeforeOrSameThread(concurrent_sid, sid)) continue;
      if (!LockSet::IntersectionIsEmpty(lsid, seg->lsid(is_w))) continue;
      if (concurrent_sids) {
        concurrent_sids->insert(concurrent_sid);
      }
      Thread *thr2 = Thread::Get(seg->tid());
      if (!printed_header) {
        Report("  %sConcurrent %s happened at (OR AFTER) these points:%s\n",
               c_magenta, descr, c_default);
        printed_header = true;
      }

      Report("   %s (%s):\n",
             thr2->ThreadName().c_str(),
             TwoLockSetsToString(seg->lsid(false),
                                 seg->lsid(true)).c_str());
      if (G_flags->show_states) {
        Report("   S%d\n", concurrent_sid.raw());
      }
      LockSet::AddLocksToSet(seg->lsid(false), locks);
      LockSet::AddLocksToSet(seg->lsid(true), locks);
      Report("%s", Segment::StackTraceString(concurrent_sid).c_str());
      if (!G_flags->pure_happens_before &&
          G_flags->suggest_happens_before_arcs) {
        set<LID> message_locks;
        // Report("Locks in T%d\n", thr1->tid().raw());
        // thr1->lock_history().PrintLocks();
        // Report("Unlocks in T%d\n", thr2->tid().raw());
        // thr2->lock_history().PrintUnlocks();
        if (LockHistory::Intersect(thr1->lock_history(), thr2->lock_history(),
                                   seg->lock_era(), &message_locks)) {
          Report("   Note: these locks were recently released by T%d"
                 " and later acquired by T%d: {%s}\n"
                 "   See http://code.google.com/p/data-race-test/wiki/"
                 "PureHappensBeforeVsHybrid\n",
                 thr2->tid().raw(),
                 thr1->tid().raw(),
                 SetOfLocksToString(message_locks).c_str());
          locks->insert(message_locks.begin(), message_locks.end());
        }
      }
    }
  }

  void SetProgramFinished() {
    CHECK(!program_finished_);
    program_finished_ = true;
  }

  string RaceInfoString(uintptr_t pc, set<SID>& concurrent_sids) {
    string s;
    char buf[100];
    snprintf(buf, 100, "Race verifier data: %p", (void*)pc);
    s += buf;
    for (set<SID>::iterator it = concurrent_sids.begin();
         it != concurrent_sids.end(); ++it) {
      // Take the first pc of the concurrent stack trace.
      uintptr_t concurrent_pc = *Segment::embedded_stack_trace(*it);
      snprintf(buf, 100, ",%p", (void*)concurrent_pc);
      s += buf;
    }
    s += "\n";
    return s;
  }

  void PrintRaceReport(ThreadSanitizerDataRaceReport *race) {
    bool short_report = program_finished_;
    if (!short_report) {
      AnnounceThreadsInSegmentSet(race->new_sval.rd_ssid());
      AnnounceThreadsInSegmentSet(race->new_sval.wr_ssid());
    }
    bool is_w = race->last_access_is_w;
    TID     tid = race->last_access_tid;
    Thread *thr = Thread::Get(tid);
    SID     sid = race->last_access_sid;
    LSID    lsid = race->last_acces_lsid[is_w];
    set<LID> all_locks;

    n_race_reports++;
    if (G_flags->html) {
      Report("<b id=race%d>Race report #%d; </b>"
             "<a href=\"#race%d\">Next;</a>  "
             "<a href=\"#race%d\">Prev;</a>\n",
             n_race_reports, n_race_reports,
             n_race_reports+1, n_race_reports-1);
    }


    // Note the {{{ and }}}. These are for vim folds.
    Report("%sWARNING: %s data race during %s of size %d at %p: {{{%s\n",
           c_red,
           race->is_expected ? "Expected" : "Possible",
           is_w ? "write" : "read",
           race->last_access_size,
           race->racey_addr,
           c_default);
    if (!short_report) {
      LockSet::AddLocksToSet(race->last_acces_lsid[false], &all_locks);
      LockSet::AddLocksToSet(race->last_acces_lsid[true], &all_locks);
      Report("   %s (%s):\n",
             thr->ThreadName().c_str(),
             TwoLockSetsToString(race->last_acces_lsid[false],
                                 race->last_acces_lsid[true]).c_str());
    }

    CHECK(race->stack_trace);
    Report("%s", race->stack_trace->ToString().c_str());
    if (short_report) {
      Report(" See the full version of this report above.\n");
      Report("}%s\n", "}}");
      return;
    }
    // Report(" sid=%d; vts=%s\n", thr->sid().raw(),
    //       thr->vts()->ToString().c_str());
    if (G_flags->show_states) {
      Report(" old state: %s\n", race->old_sval.ToString().c_str());
      Report(" new state: %s\n", race->new_sval.ToString().c_str());
    }
    set<SID> concurrent_sids;
    if (G_flags->keep_history) {
      PrintConcurrentSegmentSet(race->new_sval.wr_ssid(),
                                tid, sid, lsid, true, "write(s)", &all_locks,
                                &concurrent_sids);
      if (is_w) {
        PrintConcurrentSegmentSet(race->new_sval.rd_ssid(),
                                  tid, sid, lsid, false, "read(s)", &all_locks,
                                  &concurrent_sids);
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
      ExpectedRace *expected_race = 
          G_expected_races_map->GetInfo(race->racey_addr);
      if (expected_race) {
        CHECK(expected_race->description);
        Report(" Description: \"%s\"\n", expected_race->description);
      }
    }
    set<LID>  locks_reported;

    if (!all_locks.empty()) {
      Report("  %sLocks involved in this report "
             "(reporting last lock sites):%s {%s}\n",
             c_green, c_default,
             SetOfLocksToString(all_locks).c_str());

      for (set<LID>::iterator it = all_locks.begin();
           it != all_locks.end(); ++it) {
        LID lid = *it;
        Lock::ReportLockWithOrWithoutContext(lid, true);
      }
    }

    string raceInfoString = RaceInfoString(race->stack_trace->Get(0),
        concurrent_sids);
    Report("   %s", raceInfoString.c_str());
    Report("}}}\n");
  }

  bool PrintReport(ThreadSanitizerReport *report) {
    CHECK(report);
    // Check if we have a suppression.
    vector<string> funcs_mangled;
    vector<string> funcs_demangled;
    vector<string> objects;

    CHECK(!g_race_verifier_active);
    CHECK(report->stack_trace);
    CHECK(report->stack_trace->size());
    for (size_t i = 0; i < report->stack_trace->size(); i++) {
      uintptr_t pc = report->stack_trace->Get(i);
      string img, rtn, file;
      int line;
      PcToStrings(pc, false, &img, &rtn, &file, &line);
      funcs_mangled.push_back(rtn);
      funcs_demangled.push_back(PcToRtnName(pc, true));
      objects.push_back(img);
    }
    string suppression_name;
    if (suppressions_.StackTraceSuppressed("ThreadSanitizer",
                                           report->ReportName(),
                                           funcs_mangled,
                                           funcs_demangled,
                                           objects,
                                           &suppression_name)) {
      used_suppressions_[suppression_name]++;
      return false;
    }

    // Actually print it.
    if (report->type == ThreadSanitizerReport::UNLOCK_FOREIGN) {
      ThreadSanitizerBadUnlockReport *bad_unlock =
          reinterpret_cast<ThreadSanitizerBadUnlockReport*>(report);
      Report("WARNING: Lock %s was released by thread T%d"
             " which did not acquire this lock.\n%s",
             Lock::ToString(bad_unlock->lid).c_str(),
             bad_unlock->tid.raw(),
             bad_unlock->stack_trace->ToString().c_str());
    } else if (report->type == ThreadSanitizerReport::UNLOCK_NONLOCKED) {
      ThreadSanitizerBadUnlockReport *bad_unlock =
          reinterpret_cast<ThreadSanitizerBadUnlockReport*>(report);
      Report("WARNING: Unlocking a non-locked lock %s in thread T%d\n%s",
             Lock::ToString(bad_unlock->lid).c_str(),
             bad_unlock->tid.raw(),
             bad_unlock->stack_trace->ToString().c_str());
    } else if (report->type == ThreadSanitizerReport::INVALID_LOCK) {
      ThreadSanitizerInvalidLockReport *invalid_lock =
          reinterpret_cast<ThreadSanitizerInvalidLockReport*>(report);
      Report("WARNING: accessing an invalid lock %p in thread T%d\n%s",
             invalid_lock->lock_addr,
             invalid_lock->tid.raw(),
             invalid_lock->stack_trace->ToString().c_str());
    } else {
      CHECK(report->type == ThreadSanitizerReport::DATA_RACE);
      ThreadSanitizerDataRaceReport *race =
          reinterpret_cast<ThreadSanitizerDataRaceReport*>(report);
      PrintRaceReport(race);
    }

    n_reports++;
    SetNumberOfFoundErrors(n_reports);
    if (!G_flags->summary_file.empty()) {
      char buff[100];
      snprintf(buff, sizeof(buff),
               "ThreadSanitizer: %d warning(s) reported\n", n_reports);
      // We overwrite the contents of this file with the new summary.
      // We don't do that at the end because even if we crash later
      // we will already have the summary.
      OpenFileWriteStringAndClose(G_flags->summary_file, buff);
    }

    // Generate a suppression.
    if (G_flags->generate_suppressions) {
      string supp = "{\n";
      supp += "  <Put your suppression name here>\n";
      supp += string("  ThreadSanitizer:") + report->ReportName() + "\n";
      for (size_t i = 0; i < funcs_mangled.size(); i++) {
        const string &func = funcs_demangled[i];
        if (func.size() == 0 || func == "???") {
          supp += "  obj:" + objects[i] + "\n";
        } else {
          supp += "  fun:" + funcs_demangled[i] + "\n";
        }
        if (StackTrace::CutStackBelowFunc(funcs_demangled[i])) {
          break;
        }
      }
      supp += "}";
      Printf("------- suppression -------\n%s\n------- end suppression -------\n",
             supp.c_str());
    }

    return true;
  }

  void PrintUsedSuppression() {
    for (map<string, int>::iterator it = used_suppressions_.begin();
         it != used_suppressions_.end(); ++it) {
      Report("used_suppression: %d %s\n", it->second, it->first.c_str());
    }
  }

  void PrintSummary() {
    Report("ThreadSanitizer summary: reported %d warning(s) (%d race(s))\n",
           n_reports, n_race_reports);
  }


  string DescribeMemory(uintptr_t a) {
    const int kBufLen = 1023;
    char buff[kBufLen+1];

    // Is this stack?
    for (int i = 0; i < Thread::NumberOfThreads(); i++) {
      Thread *t = Thread::Get(TID(i));
      if (!t || !t->is_running()) continue;
      if (t->MemoryIsInStack(a)) {
        snprintf(buff, sizeof(buff),
                 "  %sLocation %p is %ld bytes inside T%d's stack [%p,%p]%s\n",
                 c_blue,
                 reinterpret_cast<void*>(a),
                 static_cast<long>(t->max_sp() - a),
                 i,
                 reinterpret_cast<void*>(t->min_sp()),
                 reinterpret_cast<void*>(t->max_sp()),
                 c_default
                );
        return buff;
      }
    }

    HeapInfo *heap_info = G_heap_map->GetInfo(a);
    if (heap_info) {
      snprintf(buff, sizeof(buff),
             "  %sLocation %p is %ld bytes inside a block starting at %p"
             " of size %ld allocated by T%d from heap:%s\n",
             c_blue,
             reinterpret_cast<void*>(a),
             static_cast<long>(a - heap_info->ptr),
             reinterpret_cast<void*>(heap_info->ptr),
             static_cast<long>(heap_info->size),
             heap_info->tid().raw(), c_default);
      return string(buff) + heap_info->StackTraceString().c_str();
    }


    // Is it a global object?
    uintptr_t offset;
    string symbol_descr;
    if (GetNameAndOffsetOfGlobalObject(a, &symbol_descr, &offset)) {
      snprintf(buff, sizeof(buff),
              "  %sAddress %p is %d bytes inside data symbol \"",
              c_blue, reinterpret_cast<void*>(a), static_cast<int>(offset));
      return buff + symbol_descr + "\"" + c_default + "\n";
    }

    if (G_flags->debug_level >= 2) {
      string res;
      // Is this near stack?
      for (int i = 0; i < Thread::NumberOfThreads(); i++) {
        Thread *t = Thread::Get(TID(i));
        const uintptr_t kMaxStackDiff = 1024 * 16;
        uintptr_t diff1 = a - t->max_sp();
        uintptr_t diff2 = t->min_sp() - a;
        if (diff1 < kMaxStackDiff ||
            diff2 < kMaxStackDiff ||
            t->MemoryIsInStack(a)) {
          uintptr_t diff = t->MemoryIsInStack(a) ? 0 :
              (diff1 < kMaxStackDiff ? diff1 : diff2);
          snprintf(buff, sizeof(buff),
                   "  %sLocation %p is within %d bytes outside T%d's stack [%p,%p]%s\n",
                   c_blue,
                   reinterpret_cast<void*>(a),
                   static_cast<int>(diff),
                   i,
                   reinterpret_cast<void*>(t->min_sp()),
                   reinterpret_cast<void*>(t->max_sp()),
                   c_default
                  );
          res += buff;
        }
      }
      if (res.size() > 0) {
        return res +
            "  This report _may_ indicate that valgrind incorrectly "
            "computed the stack boundaries\n";
      }
    }

    return "";
  }

 private:
  map<StackTrace *, int, StackTrace::Less> reported_stacks_;
  int n_reports;
  int n_race_reports;
  bool program_finished_;
  Suppressions suppressions_;
  map<string, int> used_suppressions_;
};

// -------- Event Sampling ---------------- {{{1
// This class samples (profiles) events.
// Instances of this class should all be static.
class EventSampler {
 public:

  // Sample one event
  void Sample(TID tid, const char *event_name) {
    CHECK_NE(G_flags->sample_events, 0);
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
      for (map<int, string>::iterator it = reverted_map.begin();
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

// -------- Detector ---------------------- {{{1
// Basically, a collection of event handlers.
class Detector {
 public:
  INLINE void HandleOneEvent(Event *event) {
    cur_tid_ = TID(event->tid());
    cur_thread_ = Thread::GetIfExists(cur_tid_);
    HandleOneEventInternal(event);
  }

  void INLINE HandleMemoryAccess(int32_t tid, uintptr_t pc,
                          uintptr_t addr, uintptr_t size,
                          bool is_w) {
    Thread *thr = Thread::Get(TID(tid));
    if (g_so_far_only_one_thread) return;
    HandleMemoryAccessInternal(TID(tid), thr, pc, addr, size, is_w, 
                               g_has_expensive_flags);
  }

  void INLINE HandleTraceLoop(TraceInfo *t, Thread *thr, TID tid, 
                              uintptr_t *tleb, size_t n,
                              bool has_expensive_flags) {
    size_t i = 0;
    do {
      uintptr_t addr = tleb[i];
      if (addr == 0) continue;  // This mop was not executed.
      MopInfo *mop = t->GetMop(i);
      tleb[i] = 0;  // we've consumed this mop, clear it.
      DCHECK(mop->size != 0);
      DCHECK(mop->pc != 0);
      HandleMemoryAccessInternal(tid, thr, mop->pc, addr, mop->size,
                                 mop->is_write, has_expensive_flags);
    } while (++i < n);

  }

  void INLINE HandleTrace(int32_t raw_tid, TraceInfo *t,
                          uintptr_t *tleb) {
    TID tid(raw_tid);
    Thread *thr = Thread::Get(tid);
    if (thr->ignore_all()) return;
    if (g_so_far_only_one_thread) return;
    DCHECK(t);
    if (t->generate_segments()) {
      HandleSblockEnter(tid, t->pc());
    }
    size_t n = t->n_mops();
    DCHECK(n);
    if (g_has_expensive_flags) {
      HandleTraceLoop(t, thr, tid, tleb, n, true);
    } else {
      HandleTraceLoop(t, thr, tid, tleb, n, false);
    }
  }

  void INLINE HandleStackMemChange(int32_t tid, uintptr_t addr,
                                   uintptr_t size) {
    Thread *thr = Thread::Get(TID(tid));
    if (thr->ignore_all()) return;
    G_stats->events[STACK_MEM_DIE]++;
    HandleStackMem(TID(tid), addr, size);
  }

  void ShowUnfreedHeap() {
    // check if there is not deleted memory
    // (for debugging free() interceptors, not for leak detection)
    if (DEBUG_MODE && G_flags->debug_level >= 1) {
      for (HeapMap<HeapInfo>::iterator it = G_heap_map->begin();
           it != G_heap_map->end(); ++it) {
        HeapInfo &info = it->second;
        Printf("Not free()-ed memory: %p [%p, %p)\n%s\n",
               info.size, info.ptr, info.ptr + info.size,
               info.StackTraceString().c_str());
      }
    }
  }

  void HandleProgramEnd() {
    // Report("ThreadSanitizerValgrind: done\n");
    // check if we found all expected races (for unit tests only).
    int missing = 0;
    for (ExpectedRacesMap::iterator it = G_expected_races_map->begin();
         it != G_expected_races_map->end(); ++it) {
      ExpectedRace race = it->second;
      if (race.count == 0 && !race.is_benign &&
          !(g_race_verifier_active && !race.is_verifiable)) {
        ++missing;
        Printf("Missing an expected race on %p: %s (annotated at %s)\n",
               it->first,
               race.description,
               PcToRtnNameAndFilePos(race.pc).c_str());
      }
    }

    if (missing) {
      int n_errs = GetNumberOfFoundErrors();
      SetNumberOfFoundErrors(n_errs + missing);
    }

    // ShowUnfreedHeap();
    EventSampler::ShowSamples();
    ShowStats();
    ShowProcSelfStatus();
    reports_.PrintUsedSuppression();
    reports_.PrintSummary();
    // Report("ThreadSanitizerValgrind: exiting\n");
  }

  void FlushIfOutOfMem() {
    static int max_vm_size;
    static int soft_limit;
    const int hard_limit = G_flags->max_mem_in_mb;
    const int minimal_soft_limit = (hard_limit * 13) / 16;
    const int print_info_limit   = (hard_limit * 12) / 16;

    CHECK(hard_limit > 0);

    int vm_size_in_mb = GetVmSizeInMb();
    if (max_vm_size < vm_size_in_mb) {
      max_vm_size = vm_size_in_mb;
      if (max_vm_size > print_info_limit) {
        Report("INFO: ThreadSanitizer's VmSize: %dM\n", (int)max_vm_size);
      }
    }

    if (soft_limit == 0) {
      soft_limit = minimal_soft_limit;
    }

    if (vm_size_in_mb > soft_limit) {
      ForgetAllStateAndStartOver(
          "ThreadSanitizer is running close to its memory limit");
      soft_limit = vm_size_in_mb + 1;
    }
  }

  // Force state flushing.
  void FlushState() {
    ForgetAllStateAndStartOver("State flushing requested by client");
  }

  void FlushIfNeeded() {
    // Are we out of segment IDs?
    if (Segment::NumberOfSegments() > ((kMaxSID * 15) / 16)) {
      if (DEBUG_MODE) {
        G_cache->PrintStorageStats();
        Segment::ShowSegmentStats();
      }
      ForgetAllStateAndStartOver("ThreadSanitizer has run out of segment IDs");
    }
    static int counter;
    counter++;

    // Are we out of memory?
    if (G_flags->max_mem_in_mb > 0) {
      const int kFreq = 1014 * 16;
      if ((counter % kFreq) == 0) {  // Don't do it too often.
        // TODO(kcc): find a way to check memory limit more frequently.
        FlushIfOutOfMem();
      }
    }

    size_t flush_period = G_flags->flush_period * 1000;  // milliseconds.
    if (flush_period && (counter % (1024 * 4)) == 0) {
      size_t cur_time = TimeInMilliSeconds();
      if (cur_time - g_last_flush_time  > flush_period) {
        ForgetAllStateAndStartOver(
          "Doing periodic flush (period is set by --flush_period=n_seconds)");
      }
    }
  }

  void INLINE HandleSblockEnter(TID tid, uintptr_t pc) {
    Thread *thr = Thread::Get(tid);
    if (thr->ignore_all()) return;
    thr->HandleSblockEnter(pc);
    G_stats->events[SBLOCK_ENTER]++;

    FlushIfNeeded();

    if (UNLIKELY(G_flags->sample_events)) {
      static EventSampler sampler;
      sampler.Sample(tid, "SampleSblockEnter");
    }
  }

  void HandleRtnCall(TID tid, uintptr_t call_pc, uintptr_t target_pc,
                     IGNORE_BELOW_RTN ignore_below) {
    Thread::Get(tid)->HandleRtnCall(call_pc, target_pc, ignore_below);

    FlushIfNeeded();
  }

 private:
  void ShowProcSelfStatus() {
    if (G_flags->show_proc_self_status) {
      string str = ReadFileToString("/proc/self/status", false);
      if (!str.empty()) {
        Printf("%s", str.c_str());
      }
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

    switch (type) {
      case READ:
        HandleMemoryAccess(e_->tid(), e_->pc(), e_->a(), e_->info(), false);
        break;
      case WRITE:
        HandleMemoryAccess(e_->tid(), e_->pc(), e_->a(), e_->info(), true);
        break;
      case RTN_CALL:
        HandleRtnCall(TID(e_->tid()), e_->pc(), e_->a(),
                      IGNORE_BELOW_RTN_UNKNOWN);
        break;
      case RTN_EXIT:
        Thread::Get(TID(e_->tid()))->HandleRtnExit();
        break;
      case SBLOCK_ENTER:
        HandleSblockEnter(TID(e_->tid()), e_->pc());
        break;
      case THR_START   :
        HandleThreadStart(TID(e_->tid()), TID(e_->info()), e_->pc());
        break;
      case THR_CREATE_BEFORE:
        cur_thread_->HandleThreadCreateBefore(TID(e_->tid()), e_->pc());
        break;
      case THR_CREATE_AFTER:
        cur_thread_->HandleThreadCreateAfter(TID(e_->tid()), TID(e_->info()));
        break;
      case THR_FIRST_INSN:
        HandleThreadFirstInsn(TID(e_->tid()));
        break;
      case THR_JOIN_AFTER     : HandleThreadJoinAfter();   break;
      case THR_STACK_TOP      : HandleThreadStackTop(); break;

      case THR_END     : HandleThreadEnd(TID(e_->tid()));     break;
      case MALLOC      : HandleMalloc();     break;
      case FREE        : HandleFree();         break;
      case MMAP        : HandleMalloc();      break;  // same as MALLOC
      case MUNMAP      : HandleMunmap();     break;


      case WRITER_LOCK : HandleLock(true);     break;
      case READER_LOCK : HandleLock(false);    break;
      case UNLOCK      : HandleUnlock();       break;
      case UNLOCK_OR_INIT : HandleUnlockOrInit(); break;

      case LOCK_CREATE:
      case LOCK_DESTROY: HandleLockCreateOrDestroy(); break;

      case SIGNAL      : cur_thread_->HandleSignal(e_->a());  break;
      case WAIT        : cur_thread_->HandleWait(e_->a());   break;

      case CYCLIC_BARRIER_INIT:
        cur_thread_->HandleBarrierInit(e_->a(), e_->info());
        break;
      case CYCLIC_BARRIER_WAIT_BEFORE  :
        cur_thread_->HandleBarrierWaitBefore(e_->a());
        break;
      case CYCLIC_BARRIER_WAIT_AFTER  :
        cur_thread_->HandleBarrierWaitAfter(e_->a());
        break;

      case PCQ_CREATE   : HandlePcqCreate();   break;
      case PCQ_DESTROY  : HandlePcqDestroy();  break;
      case PCQ_PUT      : HandlePcqPut();      break;
      case PCQ_GET      : HandlePcqGet();      break;


      case EXPECT_RACE :
        HandleExpectRace(false, e_->a(), e_->info(),
                         (const char*)e_->pc(), cur_tid_);
        break;
      case BENIGN_RACE :
        HandleExpectRace(true, e_->a(), e_->info(),
                         (const char*)e_->pc(), cur_tid_);
        break;
      case EXPECT_RACE_BEGIN:
        CHECK(g_expecting_races == false);
        g_expecting_races = true;
        g_found_races_since_EXPECT_RACE_BEGIN = 0;
        break;
      case EXPECT_RACE_END:
        CHECK(g_expecting_races == true);
        g_expecting_races = false;
        if (g_found_races_since_EXPECT_RACE_BEGIN == 0) {
          Printf("WARNING: expected race not found.\n");
        }
        break;

      case HB_LOCK     : HandleHBLock();       break;

      case IGNORE_READS_BEG:  HandleIgnore(false, true);  break;
      case IGNORE_READS_END:  HandleIgnore(false, false); break;
      case IGNORE_WRITES_BEG: HandleIgnore(true, true);   break;
      case IGNORE_WRITES_END: HandleIgnore(true, false);  break;

      case SET_THREAD_NAME:
        cur_thread_->set_thread_name((const char*)e_->a());
        break;
      case SET_LOCK_NAME: {
          uintptr_t lock_addr = e_->a();
          const char *name = reinterpret_cast<const char *>(e_->info());
          Lock *lock = Lock::LookupOrCreate(lock_addr);
          lock->set_name(name);
        }
        break;

      case PUBLISH_RANGE : HandlePublishRange(); break;
      case UNPUBLISH_RANGE : HandleUnpublishRange(); break;

      case TRACE_MEM   : HandleTraceMem();   break;
      case STACK_TRACE : HandleStackTrace(); break;
      case NOOP        : CHECK(0);           break;  // can't happen.
      case VERBOSITY   : e_->Print(); G_flags->verbosity = e_->info(); break;
      case FLUSH_STATE : FlushState();       break;
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

    VTS *vts = cur_thread_->segment()->vts();
    PublishRange(mem, mem + size, vts);

    cur_thread_->NewSegmentForSignal();
    // Printf("Publish: [%p, %p)\n", mem, mem+size);
  }

  // UNPUBLISH_RANGE
  void HandleUnpublishRange() {
    if (G_flags->verbosity >= 2) {
      e_->Print();
    }
    uintptr_t mem = e_->a();
    uintptr_t size = e_->info();
    UnpublishRange(mem, mem + size, cur_thread_);
  }

  void HandleIgnore(bool is_w, bool on) {
    if (G_flags->verbosity >= 2) {
      e_->Print();
    }
    cur_thread_->set_ignore(is_w, on);
  }

  // EXPECT_RACE, BENIGN_RACE
  void HandleExpectRace(bool is_benign, uintptr_t ptr, uintptr_t size,
                        const char *descr, TID tid) {
    ExpectedRace expected_race;
    expected_race.ptr = ptr;
    expected_race.size = size;
    expected_race.count = 0;
    expected_race.is_benign = is_benign;
    expected_race.is_verifiable = !descr ||
        (string(descr).find("UNVERIFIABLE") == string::npos);
    expected_race.description = descr;
    expected_race.pc = cur_thread_->GetCallstackEntry(1);
    G_expected_races_map->InsertInfo(ptr, expected_race);
    if (debug_expected_races) {
      Printf("T%d: EXPECT_RACE: ptr=%p size=%ld descr='%s' is_benign=%d\n",
             tid.raw(), ptr, size, descr, is_benign);
      cur_thread_->ReportStackTrace(ptr);
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

  void INLINE ClearMemoryStateOnStackDieMem(uintptr_t a, uintptr_t b) {
    ClearMemoryState(a, b);
  }

  void INLINE HandleStackMem(TID tid, uintptr_t addr, uintptr_t size) {
    uintptr_t a = addr;
    DCHECK(size > 0);
    DCHECK(size < 128 * 1024 * 1024);  // stay sane.
    uintptr_t b = a + size;
    ClearMemoryStateOnStackDieMem(a, b);
    if (G_flags->sample_events) {
      static EventSampler sampler;
      sampler.Sample(tid, "SampleStackChange");
    }
  }

  void HandleLock(bool is_w_lock) {
    if (debug_lock) {
      e_->Print();
      cur_thread_->ReportStackTrace();
    }
    cur_thread_->HandleLock(e_->a(), is_w_lock);
  }

  // UNLOCK
  void HandleUnlock() {
    if (debug_lock) {
      e_->Print();
      cur_thread_->ReportStackTrace();
    }
    cur_thread_->HandleUnlock(e_->a());
  }

  // UNLOCK_OR_INIT
  // This is a hack to handle posix pthread_spin_unlock which is sometimes
  // the same symbol as pthread_spin_init. We need to handle unlock as init
  // if the lock was not seen before or if it is currently unlocked.
  // TODO(kcc): is there a way to distinguish pthread_spin_init
  // and pthread_spin_unlock?
  void HandleUnlockOrInit() {
    if (G_flags->verbosity >= 2) {
      e_->Print();
      cur_thread_->ReportStackTrace();
    }
    uintptr_t lock_addr = e_->a();
    Lock *lock = Lock::Lookup(lock_addr);
    if (lock && lock->wr_held()) {
      // We know this lock and it is locked. Just unlock it.
      cur_thread_->HandleUnlock(lock_addr);
    } else {
      // Never seen this lock or it is currently unlocked. Init it.
      Lock::Create(lock_addr);
    }
  }

  void HandleLockCreateOrDestroy() {
    uintptr_t lock_addr = e_->a();
    if (debug_lock) {
      e_->Print();
    }
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
      // If the lock is not found, report an error.
      if (lock == NULL) {
        ThreadSanitizerInvalidLockReport *report =
            new ThreadSanitizerInvalidLockReport;
        report->type = ThreadSanitizerReport::INVALID_LOCK;
        report->tid = cur_tid_;
        report->lock_addr = lock_addr;
        report->stack_trace = cur_thread_->CreateStackTrace();
        ThreadSanitizerPrintReport(report);
        return;
      }
      if (lock->wr_held() || lock->rd_held()) {
        if (G_flags->unlock_on_mutex_destroy && !g_has_exited_main) {
          cur_thread_->HandleUnlock(lock_addr);
        }
      }
      Lock::Destroy(lock_addr);
    }
  }

  void HandleTraceMem() {
    if (G_flags->trace_level == 0) return;
    TID tid = cur_tid_;
    uintptr_t a = e_->a();
    CacheLine *cache_line = G_cache->GetLineOrCreateNew(a, __LINE__);
    uintptr_t offset = CacheLine::ComputeOffset(a);
    cache_line->traced().Set(offset);
    if (G_flags->verbosity >= 2) e_->Print();
  }

  INLINE void RefAndUnrefTwoSegSetPairsIfDifferent(SSID new_ssid1,
                                                   SSID old_ssid1,
                                                   SSID new_ssid2,
                                                   SSID old_ssid2) {
    bool recycle_1 = new_ssid1 != old_ssid1,
         recycle_2 = new_ssid2 != old_ssid2;
    if (recycle_1 && !new_ssid1.IsEmpty()) {
      SegmentSet::Ref(new_ssid1, "RefAndUnrefTwoSegSetPairsIfDifferent");
    }

    if (recycle_2 && !new_ssid2.IsEmpty()) {
      SegmentSet::Ref(new_ssid2, "RefAndUnrefTwoSegSetPairsIfDifferent");
    }

    if (recycle_1 && !old_ssid1.IsEmpty()) {
      SegmentSet::Unref(old_ssid1, "RefAndUnrefTwoSegSetPairsIfDifferent");
    }

    if (recycle_2 && !old_ssid2.IsEmpty()) {
      SegmentSet::Unref(old_ssid2, "RefAndUnrefTwoSegSetPairsIfDifferent");
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
        } else {
          // In happens-before mode the intersection of the LSes must be empty.
          DCHECK(!G_flags->pure_happens_before);
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
        } else {
          // In happens-before mode the intersection of the LSes must be empty.
          DCHECK(!G_flags->pure_happens_before);
        }
      }
    }
    return false;
  }

  // New experimental state machine.
  // Set *res to the new state.
  // Return true if the new state is race.
  bool INLINE MemoryStateMachine(ShadowValue old_sval, Thread *thr,
                                 bool is_w, ShadowValue *res) {
    ShadowValue new_sval;
    SID cur_sid = thr->sid();
    DCHECK(cur_sid.valid());

    if (UNLIKELY(old_sval.IsNew())) {
      // We see this memory for the first time.
      DCHECK(cur_sid.valid());
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
      if (SegmentSet::Contains(old_wr_ssid, cur_sid)) {
        // cur_sid is already in old_wr_ssid, no change to SSrd is required.
        new_rd_ssid = old_rd_ssid;
      } else {
        new_rd_ssid = SegmentSet::AddSegmentToSS(old_rd_ssid, cur_sid);
      }
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
    if (new_sval == old_sval)
      return false;

    if (new_wr_ssid.IsTuple() ||
        (!new_wr_ssid.IsEmpty() && !new_rd_ssid.IsEmpty())) {
      return CheckIfRace(new_rd_ssid, new_wr_ssid);
    }
    return false;
  }


  // Fast path implementation for the case when we stay in the same thread.
  // In this case we don't need to call HappensBefore(), deal with
  // Tuple segment sets and check for race.
  // If this function returns true, the ShadowValue *new_sval is updated
  // in the same way as MemoryStateMachine() would have done it. Just faster.
  INLINE bool MemoryStateMachineSameThread(bool is_w, ShadowValue old_sval,
                                           TID tid, SID cur_sid,
                                           ShadowValue *new_sval) {
#define MSM_STAT(i) do { if (DEBUG_MODE) \
  G_stats->msm_branch_count[i]++; } while(0)
    SSID rd_ssid = old_sval.rd_ssid();
    SSID wr_ssid = old_sval.wr_ssid();
    if (rd_ssid.IsEmpty()) {
      if (wr_ssid.IsSingleton()) {
        // *** CASE 01 ***: rd_ssid == 0, wr_ssid == singleton
        SID wr_sid = wr_ssid.GetSingleton();
        if (wr_sid == cur_sid) {  // --- w/r: {0, cur} => {0, cur}
          MSM_STAT(1);
          // no op
          return true;
        }
        if (tid == Segment::Get(wr_sid)->tid()) {
          // same thread, but the segments are different.
          DCHECK(cur_sid != wr_sid);
          if (is_w) {    // -------------- w: {0, wr} => {0, cur}
            MSM_STAT(2);
            new_sval->set(SSID(0), SSID(cur_sid));
            Segment::Unref(wr_sid, "FastPath01");
          } else {       // -------------- r: {0, wr} => {cur, wr}
            MSM_STAT(3);
            new_sval->set(SSID(cur_sid), wr_ssid);
          }
          Segment::Ref(cur_sid, "FastPath01");
          return true;
        }
      } else if (wr_ssid.IsEmpty()) {
        // *** CASE 00 ***: rd_ssid == 0, wr_ssid == 0
        if (is_w) {      // -------------- w: {0, 0} => {0, cur}
          MSM_STAT(4);
          new_sval->set(SSID(0), SSID(cur_sid));
        } else {         // -------------- r: {0, 0} => {cur, 0}
          MSM_STAT(5);
          new_sval->set(SSID(cur_sid), SSID(0));
        }
        Segment::Ref(cur_sid, "FastPath00");
        return true;
      }
    } else if (rd_ssid.IsSingleton()) {
      SID rd_sid = rd_ssid.GetSingleton();
      if (wr_ssid.IsEmpty()) {
        // *** CASE 10 ***: rd_ssid == singleton, wr_ssid == 0
        if (rd_sid == cur_sid) {
          // same segment.
          if (is_w) {    // -------------- w: {cur, 0} => {0, cur}
            MSM_STAT(6);
            new_sval->set(SSID(0), SSID(cur_sid));
          } else {       // -------------- r: {cur, 0} => {cur, 0}
            MSM_STAT(7);
            // no op
          }
          return true;
        }
        if (tid == Segment::Get(rd_sid)->tid()) {
          // same thread, but the segments are different.
          DCHECK(cur_sid != rd_sid);
          if (is_w) {  // -------------- w: {rd, 0} => {0, cur}
            MSM_STAT(8);
            new_sval->set(SSID(0), SSID(cur_sid));
          } else {     // -------------- r: {rd, 0} => {cur, 0}
            MSM_STAT(9);
            new_sval->set(SSID(cur_sid), SSID(0));
          }
          Segment::Ref(cur_sid, "FastPath10");
          Segment::Unref(rd_sid, "FastPath10");
          return true;
        }
      } else if (wr_ssid.IsSingleton()){
        // *** CASE 11 ***: rd_ssid == singleton, wr_ssid == singleton
        DCHECK(rd_ssid.IsSingleton());
        SID wr_sid = wr_ssid.GetSingleton();
        DCHECK(wr_sid != rd_sid);  // By definition of ShadowValue.
        if (cur_sid == rd_sid) {
          if (tid == Segment::Get(wr_sid)->tid()) {
            if (is_w) {  // -------------- w: {cur, wr} => {0, cur}
              MSM_STAT(10);
              new_sval->set(SSID(0), SSID(cur_sid));
              Segment::Unref(wr_sid, "FastPath11");
            } else {     // -------------- r: {cur, wr} => {cur, wr}
              MSM_STAT(11);
              // no op
            }
            return true;
          }
        } else if (cur_sid == wr_sid){
          if (tid == Segment::Get(rd_sid)->tid()) {
            if (is_w) {  // -------------- w: {rd, cur} => {rd, cur}
              MSM_STAT(12);
              // no op
            } else {     // -------------- r: {rd, cur} => {0, cur}
              MSM_STAT(13);
              new_sval->set(SSID(0), SSID(cur_sid));
              Segment::Unref(rd_sid, "FastPath11");
            }
            return true;
          }
        } else if (tid == Segment::Get(rd_sid)->tid() &&
                   tid == Segment::Get(wr_sid)->tid()) {
          if (is_w) {    // -------------- w: {rd, wr} => {0, cur}
            MSM_STAT(14);
            new_sval->set(SSID(0), SSID(cur_sid));
            Segment::Unref(wr_sid, "FastPath11");
          } else {       // -------------- r: {rd, wr} => {cur, wr}
            MSM_STAT(15);
            new_sval->set(SSID(cur_sid), wr_ssid);
          }
          Segment::Unref(rd_sid, "FastPath11");
          Segment::Ref(cur_sid, "FastPath11");
          return true;
        }
      }
    }
    MSM_STAT(0);
    return false;
#undef MSM_STAT
  }

  INLINE void HandleMemoryAccessHelper(bool is_w,
                                       CacheLine *cache_line,
                                       uintptr_t addr,
                                       uintptr_t size,
                                       TID tid,
                                       uintptr_t pc,
                                       Thread *thr) {
    uintptr_t offset = CacheLine::ComputeOffset(addr);

    ShadowValue old_sval;
    ShadowValue *sval_p = NULL;

    if (UNLIKELY(!cache_line->has_shadow_value().Get(offset))) {
      sval_p = cache_line->AddNewSvalAtOffset(offset);
      DCHECK(sval_p->IsNew());
    } else {
      sval_p = cache_line->GetValuePointer(offset);
    }
    old_sval = *sval_p;

    if (!MemoryStateMachineSameThread(is_w, old_sval, tid,
                                      thr->sid(), sval_p)) {
      bool is_published = cache_line->published().Get(offset);
      // We check only the first bit for publishing, oh well.
      if (UNLIKELY(is_published)) {
        const VTS *signaller_vts = GetPublisherVTS(addr);
        CHECK(signaller_vts);
        thr->NewSegmentForWait(signaller_vts);
      }

      bool is_race = MemoryStateMachine(old_sval, thr, is_w, sval_p);

      // Check for race.
      if (UNLIKELY(is_race)) {
        if (G_flags->report_races && !cache_line->racey().Get(offset)) {
          reports_.AddReport(tid, pc, is_w, addr, size,
                             old_sval, *sval_p, is_published);
        }
        cache_line->racey().Set(offset);
      }

      // Ref/Unref segments
      RefAndUnrefTwoSegSetPairsIfDifferent(sval_p->rd_ssid(),
                                           old_sval.rd_ssid(),
                                           sval_p->wr_ssid(),
                                           old_sval.wr_ssid());
    }


    if (DEBUG_MODE) {
      // check that the SSIDs/SIDs in the new sval have sane ref counters.
      CHECK(!sval_p->wr_ssid().IsEmpty() || !sval_p->rd_ssid().IsEmpty());
      for (int i = 0; i < 2; i++) {
        SSID ssid = i ? sval_p->rd_ssid() : sval_p->wr_ssid();
        if (ssid.IsEmpty()) continue;
        if (ssid.IsSingleton()) {
          // singleton segment should have ref count > 0.
          SID sid = ssid.GetSingleton();
          Segment *seg = Segment::Get(sid);
          CHECK(seg->ref_count() > 0);
          if (sid == thr->sid()) {
            // if this is the current seg, ref count should be > 1.
            CHECK(seg->ref_count() > 1);
          }
        } else {
          SegmentSet *sset = SegmentSet::Get(ssid);
          CHECK(sset->ref_count() > 0);
        }
      }
    }
  }

  INLINE void HandleMemoryAccessInternal(TID tid, Thread *thr, uintptr_t pc,
                                         uintptr_t addr, uintptr_t size,
                                         bool is_w, bool has_expensive_flags) {
    DCHECK(size > 0);
    DCHECK(thr->is_running());
    if (thr->ignore(is_w)) return;
    // if (thr->bus_lock_is_set()) return;

    DCHECK(thr->lsid(false) == thr->segment()->lsid(false));
    DCHECK(thr->lsid(true) == thr->segment()->lsid(true));

    uintptr_t a = addr,
              b = a + size,
              offset = CacheLine::ComputeOffset(a);

    CacheLine *cache_line = G_cache->GetLineOrCreateNew(addr, __LINE__);

    if (DEBUG_MODE && UNLIKELY(G_flags->keep_history >= 2)) {
      // Keep the precise history. Very SLOW!
      HandleSblockEnter(tid, pc);
    }

    if        (size == 8 && cache_line->SameValueStored(addr, 8)) {
      HandleMemoryAccessHelper(is_w, cache_line, addr, size, tid, pc, thr);
      if (DEBUG_MODE) G_stats->n_access8++;
    } else if (size == 4 && cache_line->SameValueStored(addr, 4)) {
      HandleMemoryAccessHelper(is_w, cache_line, addr, size, tid, pc, thr);
      if (DEBUG_MODE) G_stats->n_access4++;
    } else if (size == 2 && cache_line->SameValueStored(addr, 2)) {
      HandleMemoryAccessHelper(is_w, cache_line, addr, size, tid, pc, thr);
      if (DEBUG_MODE) G_stats->n_access2++;
    } else if (size == 1) {
      HandleMemoryAccessHelper(is_w, cache_line, addr, size, tid, pc, thr);
      if (DEBUG_MODE) G_stats->n_access1++;
    } else {
      // slow case
      for (uintptr_t x = a; x < b; x++) {
        cache_line = G_cache->GetLineOrCreateNew(x, __LINE__);
        HandleMemoryAccessHelper(is_w, cache_line, x, 1, tid, pc, thr);
        G_stats->n_access_slow++;
      }
    }

    if (has_expensive_flags) {
      G_stats->memory_access_sizes[size <= 16 ? size : 17 ]++;
      G_stats->events[is_w ? WRITE : READ]++;
      if (G_flags->trace_level >= 1) {
        bool tracing = false;
        if (cache_line->traced().Get(offset)) {
          tracing = true;
        } else if (addr == G_flags->trace_addr) {
          tracing = true;
        }
        if (tracing) {
          for (uintptr_t x = a; x < b; x++) {
            offset = CacheLine::ComputeOffset(x);
            cache_line = G_cache->GetLineOrCreateNew(x, __LINE__);
            ShadowValue *sval_p = cache_line->GetValuePointer(offset);
            if (cache_line->has_shadow_value().Get(offset) == 0) {
              continue;
            }
            bool is_published = cache_line->published().Get(offset);
            Printf("TRACE: T%d %s[%d] addr=%p sval: %s%s; line=%p (P=%s)\n",
                   tid.raw(), is_w ? "wr" : "rd",
                   size, addr, sval_p->ToString().c_str(),
                   is_published ? " P" : "",
                   cache_line,
                   cache_line->published().ToString().c_str());
            thr->ReportStackTrace(pc);
          }
        }
      }
      if (G_flags->sample_events > 0) {
        const char *type = "SampleMemoryAccess";
        static EventSampler sampler;
        sampler.Sample(tid, type);
      }
    }
  }

  void NOINLINE ClearMemoryStateOnMalloc(uintptr_t a, uintptr_t b) {
    ClearMemoryState(a, b);
  }
  void NOINLINE ClearMemoryStateOnFree(uintptr_t a, uintptr_t b) {
    ClearMemoryState(a, b);
  }

  // MALLOC
  void HandleMalloc() {
    ScopedMallocCostCenter cc("HandleMalloc");
    TID tid = cur_tid_;
    uintptr_t a = e_->a();
    uintptr_t size = e_->info();


    if (a == 0)
      return;

    #if defined(__GNUC__) && __WORDSIZE == 64
    // If we are allocating a huge piece of memory,
    // don't handle it because it is too slow.
    // TODO(kcc): this is a workaround for NaCl. May need to fix it cleaner.
    const uint64_t G84 = (1ULL << 32) * 21; // 84G.
    if (size >= G84) {
      return;
    }
    #endif

    cur_thread_->NewSegmentForMallocEvent();
    uintptr_t b = a + size;
    CHECK(a <= b);
    ClearMemoryStateOnMalloc(a, b);
    // update heap_map
    HeapInfo info;
    info.ptr  = a;
    info.size = size;
    info.sid  = cur_thread_->sid();
    Segment::Ref(info.sid, __FUNCTION__);
    if (debug_malloc) {
      Printf("T%d MALLOC: %p [%p %p) %s %s\n%s\n",
             tid.raw(), size, a, a+size,
             Segment::ToString(cur_thread_->sid()).c_str(),
             cur_thread_->segment()->vts()->ToString().c_str(),
             info.StackTraceString().c_str());
    }

    // CHECK(!G_heap_map->count(a));  // we may have two calls
                                      //  to AnnotateNewMemory.
    G_heap_map->InsertInfo(a, info);
  }

  // FREE
  void HandleFree() {
    uintptr_t a = e_->a();
    if (debug_free) {
      e_->Print();
      cur_thread_->ReportStackTrace(e_->pc());
    }
    if (a == 0)
      return;
    HeapInfo *info = G_heap_map->GetInfo(a);
    if (!info || info->ptr != a)
      return;
    uintptr_t size = info->size;
    uintptr_t max_write_size = 1024;
    // Handle the memory deletion as a write, but don't touch all
    // the memory if there is too much of it, limit with the first 1K.
    if (size && G_flags->free_is_write) {
      HandleMemoryAccess(e_->tid(), e_->pc(), a,
                         min(max_write_size, size), true /*is_w*/);
    }
    // update G_heap_map
    CHECK(info->ptr == a);
    Segment::Unref(info->sid, __FUNCTION__);

    ClearMemoryStateOnFree(a, a + size);
    G_heap_map->EraseInfo(a);
  }

  void HandleMunmap() {
    // TODO(glider): at the moment we handle only munmap()s of single mmap()ed
    // regions. The correct implementation should handle arbitrary munmap()s
    // that may carve the existing mappings or split them into two parts.
    // It should also be possible to munmap() several mappings at a time.
    uintptr_t a = e_->a();
    HeapInfo *info = G_heap_map->GetInfo(a);
    uintptr_t size = e_->info();
    if (!info || info->ptr != a || info->size != size)
      return;
    // TODO(glider): we may want to handle memory deletion and call
    // Segment::Unref for all the unmapped memory.
    Segment::Unref(info->sid, __FUNCTION__);
    G_heap_map->EraseRange(a, a + size);
  }

  void HandleThreadStart(TID child_tid, TID parent_tid, uintptr_t pc) {
    // Printf("HandleThreadStart: tid=%d parent_tid=%d pc=%lx pid=%d\n",
    //         child_tid.raw(), parent_tid.raw(), pc, getpid());
    VTS *vts = NULL;
    StackTrace *creation_context = NULL;
    if (child_tid == TID(0)) {
      // main thread, we are done.
      vts = VTS::CreateSingleton(child_tid);
    } else if (!parent_tid.valid()) {
      g_so_far_only_one_thread = false;
      Report("INFO: creating thread T%d w/o a parent\n", child_tid.raw());
      vts = VTS::CreateSingleton(child_tid);
    } else {
      g_so_far_only_one_thread = false;
      Thread *parent = Thread::Get(parent_tid);
      CHECK(parent);
      parent->HandleChildThreadStart(child_tid, &vts, &creation_context);
    }

    Thread *new_thread = new Thread(child_tid, parent_tid,
                                    vts, creation_context);
    cur_thread_ = Thread::Get(child_tid);
    CHECK(new_thread == cur_thread_);
  }

  // Executes before the first instruction of the thread but after the thread
  // has been set up (e.g. the stack is in place).
  void HandleThreadFirstInsn(TID tid) {
    // TODO(kcc): get rid of this once we find out how to get the T0's stack.
    if (tid == TID(0)) {
      uintptr_t stack_min(0), stack_max(0);
      GetThreadStack(tid.raw(), &stack_min, &stack_max);
      Thread *thr = Thread::Get(tid);
      thr->SetStack(stack_min, stack_max);
      ClearMemoryState(thr->min_sp(), thr->max_sp());
    }
  }

  // THR_STACK_TOP
  void HandleThreadStackTop() {
    Thread *thr = Thread::Get(TID(e_->tid()));
    // Stack grows from bottom up.
    uintptr_t sp = e_->a();
    uintptr_t sp_min = 0, sp_max = 0;
    uintptr_t stack_size_if_known = e_->info();
    HeapInfo *heap_info;
    if (stack_size_if_known) {
      sp_min = sp - stack_size_if_known;
      sp_max = sp;
    } else if (NULL != (heap_info = G_heap_map->GetInfo(sp))){
      if (G_flags->verbosity >= 2) {
        Printf("T%d %s: %p\n%s\n", e_->tid(), __FUNCTION__,  sp,
             reports_.DescribeMemory(sp).c_str());
      }
      sp_min = heap_info->ptr;
      sp_max = heap_info->ptr + heap_info->size;
    }
    if (debug_thread) {
      Printf("T%d SP: %p [%p %p)\n", e_->tid(), sp, sp_min, sp_max);
    }
    if (sp_min < sp_max) {
      CHECK((sp_max - sp_min) < 128 * 1024 * 1024); // stay sane.
      ClearMemoryStateOnFree(sp_min, sp_max);
      thr->SetStack(sp_min, sp_max);
    }
  }

  // THR_END
  void HandleThreadEnd(TID tid) {
    // Printf("HandleThreadEnd: %d\n", tid.raw());
    if (tid != TID(0)) {
      Thread *child = Thread::Get(tid);
      child->HandleThreadEnd();


      if (debug_thread) {
        Printf("T%d:  THR_END     : %s %s\n", tid.raw(),
               Segment::ToString(child->sid()).c_str(),
               child->vts()->ToString().c_str());
      }
      ClearMemoryState(child->min_sp(), child->max_sp());
    } else {
      reports_.SetProgramFinished();
    }


    Thread *thr = Thread::Get(tid);
    for (int rd_or_rw = 0; rd_or_rw <= 1; rd_or_rw++) {
      if (thr->ignore(rd_or_rw)) {
        Report("WARNING: T%d ended while the 'ignore %s' bit is set\n",
              tid.raw(), rd_or_rw ? "writes" : "reads");
        if (G_flags->debug_level >= 1) {
          Report("Last ignore call was here: \n%s\n",
                 thr->GetLastIgnoreContext(rd_or_rw)->ToString().c_str());
        }
      }
    }
    ShowProcSelfStatus();
  }

  // THR_JOIN_AFTER
  void HandleThreadJoinAfter() {
    TID tid = cur_tid_;
    Thread *parent_thr = Thread::Get(tid);
    VTS *vts_at_exit = NULL;
    TID child_tid = parent_thr->HandleThreadJoinAfter(&vts_at_exit, TID(e_->a()));
    CHECK(vts_at_exit);
    CHECK(parent_thr->sid().valid());
    Segment::AssertLive(parent_thr->sid(),  __LINE__);
    parent_thr->NewSegmentForWait(vts_at_exit);
    if (debug_thread) {
      Printf("T%d:  THR_JOIN_AFTER T%d  : %s\n", tid.raw(),
             child_tid.raw(), parent_thr->vts()->ToString().c_str());
    }
  }
  // data members
  Event *e_;
  TID cur_tid_;
  Thread *cur_thread_;

 public:
  // TODO(kcc): merge this into Detector class. (?)
  ReportStorage reports_;
};

static Detector        *G_detector;

// -------- Flags ------------------------- {{{1
const char *usage_str =
"Usage:\n"
"  %s [options] program_to_test [program's options]\n"
"See %s for details\n";

void ThreadSanitizerPrintUsage() {
  Printf(usage_str, G_flags->tsan_program_name.c_str(),
         G_flags->tsan_url.c_str());
}

static void ReportUnknownFlagAndExit(const string &str) {
  Printf("Unknown flag or flag value: %s\n", str.c_str());
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

static int FindBoolFlag(const char *name, bool default_val,
                  vector<string> *args, bool *retval) {
  int res = 0;
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
      else
        ReportUnknownFlagAndExit(str);
      res++;
      if (G_flags->verbosity >= 1) {
        Printf("%40s => %s\n", name, *retval ? "true" : "false");
      }
      break;
    }
    if (it != args->end()) {
      cont = true;
      args->erase(it);
    }
  } while (cont);
  return res;
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
  } while (cont);
}

static void FindUIntFlag(const char *name, intptr_t default_val,
                 vector<string> *args, uintptr_t *retval) {
  intptr_t signed_int;
  FindIntFlag(name, default_val, args, &signed_int);
  CHECK_GE(signed_int, 0);
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
  } while (cont);
}

void FindStringFlag(const char *name, vector<string> *args,
                    string *retval) {
  vector<string> tmp;
  FindStringFlag(name, args, &tmp);
  if (tmp.size() > 0) {
    *retval = tmp.back();
  }
}

static size_t GetMemoryLimitInMbFromProcSelfLimits() {
#ifdef VGO_linux
  // Parse the memory limit section of /proc/self/limits.
  string proc_self_limits = ReadFileToString("/proc/self/limits", false);
  const char *max_addr_space = "Max address space";
  size_t pos = proc_self_limits.find(max_addr_space);
  if (pos == string::npos) return 0;
  pos += strlen(max_addr_space);
  while(proc_self_limits[pos] == ' ') pos++;
  if (proc_self_limits[pos] == 'u')
    return 0;  // 'unlimited'.
  char *end;
  size_t result = my_strtol(proc_self_limits.c_str() + pos, &end);
  result >>= 20;
  return result;
#else
  return 0;
#endif
}

static size_t GetMemoryLimitInMb() {
  size_t ret = -1;  // Maximum possible value.
#if defined(VGO_linux) && __WORDSIZE == 32
  // Valgrind doesn't support more than 3G per process on 32-bit Linux.
  ret = 3 * 1024;
#endif

  // Try /proc/self/limits.
  size_t from_proc_self = GetMemoryLimitInMbFromProcSelfLimits();
  if (from_proc_self && ret > from_proc_self) {
    ret = from_proc_self;
  }
  // Try env.
  const char *from_env_str =
    (const char*)getenv("VALGRIND_MEMORY_LIMIT_IN_MB");
  if (from_env_str) {
    char *end;
    size_t from_env_value = (size_t)my_strtol(from_env_str, &end);
    if (ret > from_env_value)
      ret = from_env_value;
  }
  if (ret == (size_t)-1)
    return 0;
  return ret;
}

bool PhaseDebugIsOn(const char *phase_name) {
  CHECK(G_flags);
  for (size_t i = 0; i < G_flags->debug_phase.size(); i++) {
    if (G_flags->debug_phase[i] == phase_name)
      return true;
  }
  return false;
}

void ThreadSanitizerParseFlags(vector<string> *args) {
  // Check this first.
  FindIntFlag("v", 0, args, &G_flags->verbosity);

  FindBoolFlag("ignore_stack", true, args, &G_flags->ignore_stack);
  FindIntFlag("keep_history", 1, args, &G_flags->keep_history);
  FindUIntFlag("segment_set_recycle_queue_size", DEBUG_MODE ? 10 : 10000, args,
               &G_flags->segment_set_recycle_queue_size);
  FindUIntFlag("recent_segments_cache_size", 10, args,
               &G_flags->recent_segments_cache_size);

  bool fast_mode = false;
  FindBoolFlag("fast_mode", false, args, &fast_mode);
  if (fast_mode) {
    Printf("INFO: --fast-mode is deprecated\n");
  }
  bool ignore_in_dtor = false;
  FindBoolFlag("ignore_in_dtor", false, args, &ignore_in_dtor);
  if (ignore_in_dtor) {
    Printf("INFO: --ignore-in-dtor is deprecated\n");
  }

  int has_phb = FindBoolFlag("pure_happens_before", true, args,
                              &G_flags->pure_happens_before);
  bool hybrid = false;
  int has_hyb = FindBoolFlag("hybrid", false, args, &hybrid);
  if (has_hyb && has_phb) {
    Printf("INFO: --hybrid and --pure-happens-before"
           " is mutually exclusive; ignoring the --hybrid switch\n");
  } else if (has_hyb && !has_phb) {
    G_flags->pure_happens_before = !hybrid;
  }

  FindBoolFlag("show_expected_races", false, args,
               &G_flags->show_expected_races);
  FindBoolFlag("demangle", true, args, &G_flags->demangle);

  FindBoolFlag("announce_threads", false, args, &G_flags->announce_threads);
  FindBoolFlag("full_output", false, args, &G_flags->full_output);
  FindBoolFlag("show_states", false, args, &G_flags->show_states);
  FindBoolFlag("show_proc_self_status", false, args,
               &G_flags->show_proc_self_status);
  FindBoolFlag("show_valgrind_context", false, args,
               &G_flags->show_valgrind_context);
  FindBoolFlag("suggest_happens_before_arcs", true, args,
               &G_flags->suggest_happens_before_arcs);
  FindBoolFlag("show_pc", false, args, &G_flags->show_pc);
  FindBoolFlag("free_is_write", false, args, &G_flags->free_is_write);
  FindBoolFlag("exit_after_main", false, args, &G_flags->exit_after_main);

  FindBoolFlag("show_stats", false, args, &G_flags->show_stats);
  FindBoolFlag("color", false, args, &G_flags->color);
  FindBoolFlag("html", false, args, &G_flags->html);

  FindIntFlag("dry_run", 0, args, &G_flags->dry_run);
  FindBoolFlag("report_races", true, args, &G_flags->report_races);
  FindBoolFlag("separate_analysis_thread", false, args,
               &G_flags->separate_analysis_thread);
  FindBoolFlag("compress_cache_lines", false, args,
               &G_flags->compress_cache_lines);
  FindBoolFlag("unlock_on_mutex_destroy", true, args,
               &G_flags->unlock_on_mutex_destroy);

  FindIntFlag("sample_events", 0, args, &G_flags->sample_events);
  FindIntFlag("sample_events_depth", 2, args, &G_flags->sample_events_depth);

  FindIntFlag("debug_level", 1, args, &G_flags->debug_level);
  FindStringFlag("debug_phase", args, &G_flags->debug_phase);
  FindIntFlag("trace_level", 0, args, &G_flags->trace_level);

  FindIntFlag("literace_sampling", 0, args, &G_flags->literace_sampling);
  CHECK(G_flags->literace_sampling < 32);

  FindStringFlag("file_prefix_to_cut", args, &G_flags->file_prefix_to_cut);
  for (size_t i = 0; i < G_flags->file_prefix_to_cut.size(); i++) {
    G_flags->file_prefix_to_cut[i] =
        ConvertToPlatformIndependentPath(G_flags->file_prefix_to_cut[i]);
  }

  FindStringFlag("ignore", args, &G_flags->ignore);

  FindBoolFlag("thread_coverage", false, args, &G_flags->thread_coverage);
  FindBoolFlag("call_coverage", false, args, &G_flags->call_coverage);
  FindStringFlag("dump_events", args, &G_flags->dump_events);
  FindBoolFlag("symbolize", true, args, &G_flags->symbolize);

  FindIntFlag("trace_addr", 0, args,
              reinterpret_cast<intptr_t*>(&G_flags->trace_addr));

  FindIntFlag("max_mem_in_mb", 0, args, &G_flags->max_mem_in_mb);
  FindBoolFlag("offline", false, args, &G_flags->offline);
  FindBoolFlag("attach_mode", false, args, &G_flags->attach_mode);
  if (G_flags->max_mem_in_mb == 0) {
    G_flags->max_mem_in_mb = GetMemoryLimitInMb();
  }

  vector<string> summary_file_tmp;
  FindStringFlag("summary_file", args, &summary_file_tmp);
  if (summary_file_tmp.size() > 0) {
    G_flags->summary_file = summary_file_tmp.back();
  }

  vector<string> log_file_tmp;
  FindStringFlag("log_file", args, &log_file_tmp);
  if (log_file_tmp.size() > 0) {
    G_flags->log_file = log_file_tmp.back();
  }

  vector<string> offline_syntax_temp;
  FindStringFlag("offline_syntax", args, &offline_syntax_temp);
  if (offline_syntax_temp.size() > 0) {
    G_flags->offline_syntax = offline_syntax_temp.back();
  }

  G_flags->tsan_program_name = "valgrind --tool=tsan";
  FindStringFlag("tsan_program_name", args, &G_flags->tsan_program_name);

  G_flags->tsan_url = "http://code.google.com/p/data-race-test";
  FindStringFlag("tsan_url", args, &G_flags->tsan_url);

  FindStringFlag("suppressions", args, &G_flags->suppressions);
  FindBoolFlag("gen_suppressions", false, args,
               &G_flags->generate_suppressions);

  FindIntFlag("error_exitcode", 0, args, &G_flags->error_exitcode);
  FindIntFlag("flush_period", 0, args, &G_flags->flush_period);
  FindBoolFlag("trace_children", false, args, &G_flags->trace_children);

  FindIntFlag("max_sid", kMaxSID, args, &G_flags->max_sid);
  kMaxSID = G_flags->max_sid;
  if (kMaxSID <= 100000) {
    Printf("Error: max-sid should be at least 100000. Exiting\n");
    exit(1);
  }

  FindIntFlag("num_callers_in_history", kSizeOfHistoryStackTrace, args,
              &G_flags->num_callers_in_history);
  kSizeOfHistoryStackTrace = G_flags->num_callers_in_history;

  // Cut stack under the following default functions.
  G_flags->cut_stack_below.push_back("Thread*ThreadBody*");
  G_flags->cut_stack_below.push_back("ThreadSanitizerStartThread");
  G_flags->cut_stack_below.push_back("start_thread");
  G_flags->cut_stack_below.push_back("BaseThreadInitThunk");
  FindStringFlag("cut_stack_below", args, &G_flags->cut_stack_below);

  FindIntFlag("num_callers", 12, args, &G_flags->num_callers);

  G_flags->max_n_threads        = 100000;

  if (G_flags->full_output) {
    G_flags->announce_threads = true;
    G_flags->show_pc = true;
    G_flags->show_states = true;
    G_flags->file_prefix_to_cut.clear();
  }

  FindIntFlag("race_verifier_sleep_ms", 100, args,
      &G_flags->race_verifier_sleep_ms);
  FindStringFlag("race_verifier", args, &G_flags->race_verifier);
  FindStringFlag("race_verifier_extra", args, &G_flags->race_verifier_extra);
  g_race_verifier_active =
      !(G_flags->race_verifier.empty() && G_flags->race_verifier_extra.empty());
  if (g_race_verifier_active) {
    Printf("INFO: ThreadSanitizer running in Race Verifier mode.\n");
  }

  if (!args->empty()) {
    ReportUnknownFlagAndExit(args->front());
  }

  debug_expected_races = PhaseDebugIsOn("expected_races");
  debug_malloc = PhaseDebugIsOn("malloc");
  debug_free = PhaseDebugIsOn("free");
  debug_thread = PhaseDebugIsOn("thread");
  debug_ignore = PhaseDebugIsOn("ignore");
  debug_rtn = PhaseDebugIsOn("rtn");
  debug_lock = PhaseDebugIsOn("lock");
  debug_wrap = PhaseDebugIsOn("wrap");
  debug_ins = PhaseDebugIsOn("ins");
  debug_shadow_stack = PhaseDebugIsOn("shadow_stack");
  debug_happens_before = PhaseDebugIsOn("happens_before");
  debug_cache = PhaseDebugIsOn("cache");
  debug_race_verifier = PhaseDebugIsOn("race_verifier");

  g_has_expensive_flags = G_flags->trace_level > 0 ||
      G_flags->show_stats                          ||
      G_flags->sample_events > 0;
}

// -------- ThreadSanitizer ------------------ {{{1

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
    if (ch == '#') {
      in_comment = true;
      continue;
    }
    if (!in_comment) {
      cur_line += ch;
    }
  }
}

struct IgnoreLists {
  vector<string> funs;
  vector<string> funs_r;
  vector<string> funs_hist;
  vector<string> objs;
  vector<string> files;
};

static IgnoreLists *g_ignore_lists;

bool ReadIgnoreLine(string input_line, string prefix,
                    vector<string> &output_list) {
  if (input_line.find(prefix) != 0)
    return false;
  string s = input_line.substr(prefix.size());
  output_list.push_back(s);
  return true;
}

// Setup the list of functions/images/files to ignore.
static void SetupIgnore() {
  g_ignore_lists = new IgnoreLists;
  // add some major ignore entries so that tsan remains sane
  // even w/o any ignore file.
  g_ignore_lists->objs.push_back("*/libpthread-*");
  g_ignore_lists->objs.push_back("*/libpthread.so*");
  g_ignore_lists->objs.push_back("*/ld-2*.so");

  g_ignore_lists->objs.push_back("*ole32.dll");
  g_ignore_lists->objs.push_back("*OLEAUT32.dll");
  g_ignore_lists->objs.push_back("*MSCTF.dll");
  g_ignore_lists->objs.push_back("*ntdll.dll");
  g_ignore_lists->objs.push_back("*ntdll.dll.so");
  g_ignore_lists->objs.push_back("*mswsock.dll");
  g_ignore_lists->objs.push_back("*WS2_32.dll");
  g_ignore_lists->objs.push_back("*msvcrt.dll");
  g_ignore_lists->objs.push_back("*kernel32.dll");
  g_ignore_lists->objs.push_back("*ADVAPI32.DLL");

#ifdef VGO_darwin
  g_ignore_lists->objs.push_back("/usr/lib/dyld");
  g_ignore_lists->objs.push_back("/usr/lib/libobjc.A.dylib");
  g_ignore_lists->objs.push_back("*/libSystem.*.dylib");
#endif

  g_ignore_lists->funs.push_back("pthread_create*");
  g_ignore_lists->funs.push_back("exit");
  g_ignore_lists->funs.push_back("__cxa_*");
  g_ignore_lists->funs.push_back("__lll_mutex_unlock_wake");
  g_ignore_lists->funs.push_back("__sigsetjmp");
  g_ignore_lists->funs.push_back("__sigjmp_save");
  g_ignore_lists->funs.push_back("_setjmp");
  g_ignore_lists->funs.push_back("_longjmp_unwind");
  g_ignore_lists->funs.push_back("longjmp");
  g_ignore_lists->funs.push_back("_EH_epilog3");
  g_ignore_lists->funs.push_back("_EH_prolog3_catch");

#ifdef VGO_darwin
  g_ignore_lists->funs_r.push_back("__CFDoExternRefOperation");
  g_ignore_lists->funs_r.push_back("_CFAutoreleasePoolPop");
  g_ignore_lists->funs_r.push_back("_CFAutoreleasePoolPush");

  // pthread_lib_{enter,exit} shouldn't give us any reports since they
  // have IGNORE_ALL_ACCESSES_BEGIN/END but they do give the reports...
  g_ignore_lists->funs_r.push_back("pthread_lib_enter");
  g_ignore_lists->funs_r.push_back("pthread_lib_exit");
#endif

  // do not create segments in our Replace_* functions
  g_ignore_lists->funs_hist.push_back("Replace_memcpy");
  g_ignore_lists->funs_hist.push_back("Replace_memchr");
  g_ignore_lists->funs_hist.push_back("Replace_strcpy");
  g_ignore_lists->funs_hist.push_back("Replace_strchr");
  g_ignore_lists->funs_hist.push_back("Replace_strrchr");
  g_ignore_lists->funs_hist.push_back("Replace_strlen");
  g_ignore_lists->funs_hist.push_back("Replace_strcmp");

  // Ignore everything in our own file.
  g_ignore_lists->files.push_back("*ts_valgrind_intercepts.c");

  // Now read the ignore files.
  for (size_t i = 0; i < G_flags->ignore.size(); i++) {
    string file_name = G_flags->ignore[i];
    Report("INFO: Reading ignore file: %s\n", file_name.c_str());
    string str = ReadFileToString(file_name, true);
    vector<string> lines;
    SplitStringIntoLinesAndRemoveBlanksAndComments(str, &lines);
    for (size_t j = 0; j < lines.size(); j++) {
      string &line = lines[j];
      bool line_parsed =
          ReadIgnoreLine(line, "obj:", g_ignore_lists->objs) ||
          ReadIgnoreLine(line, "src:", g_ignore_lists->files) ||
          ReadIgnoreLine(line, "fun:",      g_ignore_lists->funs) ||
          ReadIgnoreLine(line, "fun_r:",    g_ignore_lists->funs_r) ||
          ReadIgnoreLine(line, "fun_hist:", g_ignore_lists->funs_hist);
      if (!line_parsed) {
        Printf("Error reading ignore file line:\n%s\n", line.c_str());
        CHECK(0);
      }
    }
  }

  for (size_t i = 0; i < g_ignore_lists->files.size(); i++) {
    string &cur = g_ignore_lists->files[i];
    cur = ConvertToPlatformIndependentPath(cur);
  }
}

bool StringVectorMatch(const vector<string>& v, const string& s) {
  for (size_t i = 0; i < v.size(); i++) {
    if (StringMatch(v[i], s))
      return true;
  }
  return false;
}

bool ThreadSanitizerWantToInstrumentSblock(uintptr_t pc) {
  string img_name, rtn_name, file_name;
  int line_no;
  G_stats->pc_to_strings++;
  PcToStrings(pc, false, &img_name, &rtn_name, &file_name, &line_no);

  bool ret = !(StringVectorMatch(g_ignore_lists->files, file_name)
        || StringVectorMatch(g_ignore_lists->objs, img_name)
        || StringVectorMatch(g_ignore_lists->funs, rtn_name)
        || StringVectorMatch(g_ignore_lists->funs_r, rtn_name));
  if (0) {
    Printf("%s: pc=%p file_name=%s img_name=%s rtn_name=%s ret=%d\n",
           __FUNCTION__, pc, file_name.c_str(), img_name.c_str(),
           rtn_name.c_str(), ret);
  }
  return ret;
}

bool ThreadSanitizerWantToCreateSegmentsOnSblockEntry(uintptr_t pc) {
  string rtn_name;
  rtn_name = PcToRtnNameWithStats(pc, false);

  return !(StringVectorMatch(g_ignore_lists->funs_hist, rtn_name));
}

// Returns true if function at "pc" is marked as "fun_r" in the ignore file.
bool NOINLINE ThreadSanitizerIgnoreAccessesBelowFunction(uintptr_t pc) {
  typedef hash_map<uintptr_t, bool> Cache;
  static Cache *cache = NULL;
  if (!cache)
    cache = new Cache;

  // Fast path - check if we already know the answer.
  Cache::iterator i = cache->find(pc);
  if (i != cache->end())
    return i->second;

  string rtn_name = PcToRtnNameWithStats(pc, false);
  bool ret = StringVectorMatch(g_ignore_lists->funs_r, rtn_name);
  if (ret && debug_ignore) {
    Report("INFO: ignoring all accesses below the function '%s' (%p)\n",
           PcToRtnNameAndFilePos(pc).c_str(), pc);
  }
  return ((*cache)[pc] = ret);
}

// We intercept a user function with this name
// and answer the user query with a non-NULL string.
const char *ThreadSanitizerQuery(const char *query) {
  const char *ret = "0";
  string str(query);
  if (str == "pure_happens_before" && G_flags->pure_happens_before == true) {
    ret = "1";
  }
  if (str == "hybrid_full" &&
      G_flags->pure_happens_before == false) {
    ret = "1";
  }
  if (str == "race_verifier" && g_race_verifier_active == true) {
    ret = "1";
  }
  if (DEBUG_MODE && G_flags->debug_level >= 2) {
    Printf("ThreadSanitizerQuery(\"%s\") = \"%s\"\n", query, ret);
  }
  return ret;
}

extern void ThreadSanitizerInit() {
  ScopedMallocCostCenter cc("ThreadSanitizerInit");
  g_so_far_only_one_thread = true;
  CHECK_EQ(sizeof(ShadowValue), 8);
  CHECK(G_flags);
  G_stats        = new Stats;
  SetupIgnore();

  G_detector     = new Detector;
  G_cache        = new Cache;
  G_expected_races_map = new ExpectedRacesMap;
  G_heap_map           = new HeapMap<HeapInfo>;
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

extern void ThreadSanitizerDumpAllStacks() {
  // first, print running threads.
  for (int i = 0; i < Thread::NumberOfThreads(); i++) {
    Thread *t = Thread::Get(TID(i));
    if (!t || !t->is_running()) continue;
    Report("T%d\n", i);
    t->ReportStackTrace();
  }
  // now print all dead threds.
  for (int i = 0; i < Thread::NumberOfThreads(); i++) {
    Thread *t = Thread::Get(TID(i));
    if (!t || t->is_running()) continue;
    Report("T%d (not running)\n", i);
    t->ReportStackTrace();
  }
}


extern void ThreadSanitizerHandleOneEvent(Event *e) {
  G_detector->HandleOneEvent(e);
}

void INLINE ThreadSanitizerHandleMemoryAccess(int32_t tid, uintptr_t pc,
                                              uintptr_t addr, uintptr_t size,
                                              bool is_w) {
  G_detector->HandleMemoryAccess(tid, pc, addr, size, is_w);
}

extern INLINE void ThreadSanitizerHandleTrace(int32_t tid, TraceInfo *trace_info,
                                       uintptr_t *tleb) {
  G_detector->HandleTrace(tid, trace_info, tleb);
}

void INLINE ThreadSanitizerHandleStackMemChange(int32_t tid, uintptr_t addr,
                                                uintptr_t size) {
  G_detector->HandleStackMemChange(tid, addr, size);
}

void INLINE ThreadSanitizerEnterSblock(int32_t tid, uintptr_t pc) {
  G_detector->HandleSblockEnter(TID(tid), pc);
}

void INLINE ThreadSanitizerHandleRtnCall(int32_t tid, uintptr_t call_pc,
                                         uintptr_t target_pc,
                                         IGNORE_BELOW_RTN ignore_below) {
  G_detector->HandleRtnCall(TID(tid), call_pc, target_pc, ignore_below);
}
void INLINE ThreadSanitizerHandleRtnExit(int32_t tid) {
  Thread::Get(TID(tid))->HandleRtnExit();
}

static bool ThreadSanitizerPrintReport(ThreadSanitizerReport *report) {
  return G_detector->reports_.PrintReport(report);
}

// -------- TODO -------------------------- {{{1
// - Support configurable aliases for function names (is it doable in valgrind)?
// - Correctly support atomic operations (not just ignore).
// - Handle INC as just one write
//   - same for memset, etc
// - Implement correct handling of memory accesses with different sizes.
// - Do not create HB arcs between RdUnlock and RdLock
// - Compress cache lines
// - Optimize the case where a threads signals twice in a row on the same
//   address.
// - Fix --ignore-in-dtor if --demangle=no.
// - Use cpplint (http://code.google.com/p/google-styleguide)
// - Get rid of annoying casts in printfs.
// - Compress stack traces (64-bit only. may save up to 36 bytes per segment).
// end. {{{1
#endif  // INCLUDE_THREAD_SANITIZER_CC
// vim:shiftwidth=2:softtabstop=2:expandtab:tw=80
