// Copyright 2011 Google Inc. All Rights Reserved.
// Author: glider@google.com (Alexander Potapenko)

#include "llvm/ADT/SmallSet.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/DebugInfo.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"

#include <map>
#include <set>
#include <string>
#include <vector>

#include "ignore.h"

#define UNIMPLEMENTED() CHECK(0 && "UNIMPLEMENTED" && __FILE__ && __LINE__)

namespace {

typedef std::vector <llvm::Constant*> Passport;
struct DebugPcInfo {
  DebugPcInfo(std::string s, std::string p, std::string f, uintptr_t l)
      : symbol(s), path(p), file(f), line(l) { }
  std::string symbol;
  std::string path;
  std::string file;
  uintptr_t line;
};

typedef llvm::SmallSet<llvm::Instruction*, 32> InstSet;
typedef llvm::SmallSet<llvm::BasicBlock*, 16> BlockSet;
typedef std::vector<llvm::BasicBlock*> BlockVector;

struct Trace {
  BlockSet blocks;
  llvm::BasicBlock *entry;
  BlockSet exits;
  InstSet mops_to_instrument;
  int num_mops;

  Trace() : num_mops(0) {}
};

typedef std::vector<Trace*> TraceVector;

struct InstrumentationStats {
  enum { kNumStats = 20 };
  InstrumentationStats();
  void newFunction();
  void newTrace();
  void newBasicBlocks(int num);
  void newMop();
  void newInstrumentedTrace();
  void newInstrumentedBasicBlock();
  void newInstrumentedMop();
  void newIgnoredInlinedMop();
  void newMopUninstrumentedByAA();
  void newMopUninstrumentedByFlag();
  void finalize();
  void printStats();

  std::vector<int> traces_bbs, traces_mops;
  // numbers
  int num_functions;
  int num_traces;
  int num_inst_traces;
  int num_inst_traces_in_function;
  int num_bbs;
  int num_mops;
  int num_inst_bbs;
  int num_inst_bbs_in_trace;
  int num_inst_mops;
  int num_inst_mops_in_trace;

  int num_traces_with_n_inst_bbs[kNumStats];
  // uninstrumented mops
  int num_uninst_mops;
  int num_uninst_mops_ignored;
  int num_uninst_mops_aa;
  int num_uninst_mops_flag;

  // medians
  int med_trace_size_bbs;
  int med_trace_size_mops;

  // maximums
  int max_trace_size_bbs;
  int max_trace_size_mops;
};

struct TsanOnlineInstrument : public llvm::ModulePass { // {{{1
  TsanOnlineInstrument();
  virtual bool runOnModule(llvm::Module &M);
  virtual const char *getPassName() const;
  void runOnFunction(llvm::Module::iterator &F);
  void runOnTrace(Trace &trace, bool first_dtor_bb);
  void runOnBasicBlock(llvm::BasicBlock *BB,
                       bool first_dtor_bb,
                       Trace &trace,
                       bool useTLEB);
  llvm::Constant *getInstructionAddr(int mop_index,
                                     llvm::BasicBlock::iterator &cur_inst,
                                     const llvm::IntegerType *ResultType);
  void parseIgnoreFile(std::string &file);
  llvm::DILocation getTopInlinedLocation(llvm::BasicBlock::iterator &BI);
  void dumpInstructionDebugInfo(llvm::Constant *addr,
                                const llvm::BasicBlock::iterator BI);
  uintptr_t getModuleID(llvm::Module &M);
  void setupFlags();
  void setupDataTypes();
  void setupRuntimeGlobals();
  bool isDtor(const std::string &mangled_name);
  void writeModuleDebugInfo(llvm::Module &M);
  BlockSet &getPredecessors(llvm::BasicBlock *bb);
  bool visit(llvm::BasicBlock *node, Trace &trace, BlockSet &visited);
  bool traceHasCycles(Trace &trace);
  bool validateTrace(Trace &trace);
  void buildClosureInner(Trace &trace, BlockSet &used);
  void buildClosure(Trace &trace, BlockSet &used);
  void cachePredecessors(llvm::Function &F);
  TraceVector buildTraces(llvm::Function &F);
  bool isaCallOrInvoke(llvm::BasicBlock::iterator &BI);
  int numMopsInFunction(llvm::Module::iterator &F);
  int getMopPtrSize(llvm::Value *mopPtr, bool isStore);
  bool ignoreInlinedMop(llvm::BasicBlock::iterator &BI);
  void markMopsToInstrument(Trace &trace);
  bool makeTracePassport(Trace &trace);
  bool shouldIgnoreFunction(llvm::Function &F);
  bool shouldIgnoreFunctionRecursively(llvm::Function &F);
  // Instrumentation routines.
  void insertRtnCall(llvm::Constant *addr,
                     llvm::BasicBlock::iterator &Before);
  void insertRtnExit(llvm::BasicBlock::iterator &Before);
  void writeRtnExitToTleb(llvm::BasicBlock::iterator &Before);
  void writeRtnCallToTleb(llvm::BasicBlock::iterator &Before);
  void writeSblockEnterToTleb(llvm::BasicBlock::iterator &Before);
  void insertIgnoreInc(llvm::BasicBlock::iterator &Before);
  void insertIgnoreDec(llvm::BasicBlock::iterator &Before);
  // TODO(glider): deprecate and delete.
  //void insertFlushCall(Trace &trace, llvm::Instruction *Before);
  void insertFlushCurrentCall(Trace &trace, llvm::Instruction *Before,
                              bool useTLEB, llvm::Value *MopAddr);
  void insertMaybeFlushTleb(Trace &trace, llvm::Instruction *Before);
  bool instrumentMop(llvm::BasicBlock::iterator &BI,
                     bool isStore,
                     bool check_ident_store,
                     Trace &trace,
                     bool useTLEB);
  void instrumentMemTransfer(llvm::BasicBlock::iterator &BI);
  void instrumentCall(llvm::BasicBlock::iterator &BI);

  static char ID; // Pass identification, replacement for typeid
  IgnoreLists Ignores;
  int ArchSize;
  int ModuleID;
  int ModuleFunctionCount, ModuleMopCount, FunctionMopCount, TLEBIndex,
      FunctionMopCountOnTrace;
  int TraceNumMops, InstrumentedTraceCount;
  llvm::Value *TracePassportGlob;
  llvm::GlobalVariable *LiteRaceStorageGlob;
  // Functions provided by the RTL.
  llvm::Constant *BBFlushCurrentFn, *BBFlushMop;
  llvm::Constant *RtnCallFn, *RtnExitFn, *ShadowStackCheckFn;
  llvm::Constant *MemCpyFn, *MemMoveFn, *MemSetIntrinsicFn;
  // Basic types.
  const llvm::PointerType *UIntPtr, *TraceInfoTypePtr, *Int8Ptr;
  const llvm::IntegerType *PlatformInt, *PlatformPc, *ArithmeticPtr, *Int64;
  const llvm::Type *Int1, *Int4, *Int8, *Int32;
  const llvm::Type *Void;
  // Compound types.
  const llvm::StructType *MopType, *TraceInfoType, *BBTraceInfoType;
  const llvm::IntegerType *MopType64;
  const llvm::StructType *LiteRaceCountersType;
  const llvm::ArrayType *LiteRaceStorageType, *LiteRaceStorageLineType;
  const llvm::PointerType *LiteRaceStoragePtrType;
  const llvm::ArrayType *MopArrayType;
  const llvm::ArrayType *LiteRaceCountersArrayType, *LiteRaceSkipArrayType;
  const llvm::ArrayType *TracePassportType, *TraceExtPassportType;
  const llvm::Type *TLEBTy;
  const llvm::PointerType *TLEBPtrTy;
  const llvm::StructType *CallStackType;
  const llvm::ArrayType *CallStackArrayType;

  // Globals provided by the RTL.
  llvm::Value *ShadowStack, *CurrentStackEnd, *TLEB, *DTLEB, *DTlebTop, *LiteraceTid;
  llvm::Value *ThreadLocalIgnore;

  llvm::AliasAnalysis *AA;
  llvm::TargetData *TD;

  // Constants.
  static const int kTLEBSize = 100;
  // TODO(glider): hashing constants and BB addresses should be different on
  // x86 and x86-64.
  static const int kBBHiAddr = 2048, kBBLoAddr = 128;
  //static const int kFNV1aPrime = 6733, kFNV1aModulo = 2048;
  // TODO(glider): these numbers are in fact unfair, see
  // http://isthe.com/chongo/tech/comp/fnv/index.html
  static const int kFNV1aPrime = 104729, kFNV1aModulo = 65536;
  static const int kMaxAddr = 1 << 30;
  static const int kDebugInfoMagicNumber = 0xdb914f0;
  // TODO(glider): must be in sync with ts_trace_info.h
  static const int kLiteRaceNumTids = 8;
  static const int kLiteRaceStorageSize = 8;
  static const size_t kMaxCallStackSize = 1 << 12;
  // Debug info.
  InstrumentationStats instrumentation_stats;
  std::set<std::string> debug_symbol_set;
  std::set<std::string> debug_file_set;
  std::set<std::string> debug_path_set;
  std::map<llvm::Constant*, DebugPcInfo> debug_pc_map;

  // TODO(glider): box the trace into a class that provides the set of
  // predecessors.
  std::map<llvm::BasicBlock*, BlockSet> predecessors;
  llvm::Module *ThisModule;
  llvm::LLVMContext *ThisModuleContext;
private:
  virtual void getAnalysisUsage(llvm::AnalysisUsage &AU) const;

  InstSet calls_to_instrument;
};  // }}}

}  // namespace
