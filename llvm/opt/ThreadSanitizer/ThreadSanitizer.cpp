#define DEBUG_TYPE "tsan"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/DebugInfo.h"
#include "llvm/CallingConv.h"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/InlineAsm.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instructions.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Type.h"

#include <stdint.h>
#include <stdio.h>

#include <map>
#include <set>
using namespace llvm;
using namespace std;

#if defined(__GNUC__)
# include <cxxabi.h>  // __cxa_demangle
#endif

#define DEBUG 0

// Command-line flags.
static cl::opt<std::string>
    TargetArch("arch", cl::desc("Target arch: x86 or x64"));
static cl::opt<bool>
    InstrumentAll("instrument-all",
                  cl::desc("Do not optimize the instrumentation "
                           "of memory operations"));
static cl::opt<bool>
    PrintStats("print-stats",
                  cl::desc("Print the instrumentation stats"),
                  cl::init(false));

static cl::opt<bool>
    WorkaroundVptrRace("workaround-vptr-race",
                       cl::desc("Work around benign races on VPTR in "
                                "destructors"),
                       cl::init(true));

static cl::opt<bool>
    EnableLiteraceSampling("enable-literace-sampling",
                           cl::desc("Flush hot traces less frequently"),
                           cl::init(true));

namespace {
  typedef std::vector <Constant*> Passport;
  struct DebugPcInfo {
    DebugPcInfo(string s, string p, string f, uintptr_t l)
        : symbol(s), path(p), file(f), line(l) { }
    string symbol;
    string path;
    string file;
    uintptr_t line;
  };

  typedef set<BasicBlock*> BlockSet;
  typedef vector<BasicBlock*> BlockVector;
  typedef set<Instruction*> InstSet;

  struct Trace {
    BlockSet blocks;
    BasicBlock *entry;
    set<BasicBlock*> exits;
    InstSet to_instrument;
    int num_mops;

    Trace() : num_mops(0) {}
  };

  struct InstrumentationStats {
    InstrumentationStats();
    void newFunction();
    void newTrace();
    void newBasicBlocks(int num);
    void newInstrumentedBasicBlock();
    void newMop();
    void finalize();
    void printStats();

    vector<int> traces;
    // numbers
    int num_functions;
    int num_traces;
    int num_bbs;
    int num_inst_bbs;
    int num_inst_bbs_in_trace;
    int num_mops;

    // medians
    int med_trace_size;
  };

  typedef vector<Trace> TraceVector;

  struct TsanOnlineInstrument : public ModulePass { // {{{1
    static char ID; // Pass identification, replacement for typeid
    int ModuleFunctionCount, ModuleMopCount, FunctionMopCount, TLEBIndex,
        FunctionMopCountOnTrace;
    Value *TracePassportGlob;
    GlobalVariable *LiteRaceStorageGlob;
    int TraceCount, TraceNumMops, InstrumentedTraceCount;
    Constant *BBFlushFn, *BBFlushCurrentFn;
    // TODO(glider): get rid of rtn_call/rtn_exit at all.
    Constant *RtnCallFn, *RtnExitFn;
    Constant *MemCpyFn, *MemMoveFn, *MemSetIntrinsicFn;
    const PointerType *UIntPtr, *TraceInfoTypePtr, *Int8Ptr;
    const Type *PlatformInt, *Int8, *ArithmeticPtr, *Int32;
    const Type *Void;
    const StructType *MopType, *TraceInfoType, *BBTraceInfoType;
    const StructType *LiteRaceCountersType;
    const ArrayType *LiteRaceStorageType, *LiteRaceStorageLineType;
    const PointerType *LiteRaceStoragePtrType;
    const ArrayType *MopArrayType;
    const ArrayType *LiteRaceCountersArrayType, *LiteRaceSkipArrayType;
    const ArrayType *TracePassportType, *TraceExtPassportType;
    const Type *TLEBTy;
    const PointerType *TLEBPtrType;
    Value *ShadowStack, *CurrentStackEnd, *TLEB, *LiteraceTid;
    const StructType *CallStackType;
    const ArrayType *CallStackArrayType;
    AliasAnalysis *AA;
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
    int arch_size_;
    int ModuleID;
    InstrumentationStats instrumentation_stats;
    set<string> debug_symbol_set;
    set<string> debug_file_set;
    set<string> debug_path_set;
    map<Constant*, DebugPcInfo> debug_pc_map;

    // TODO(glider): box the trace into a class that provides the set of
    // predecessors.
    map<BasicBlock*, BlockSet> predecessors;

    TsanOnlineInstrument() : ModulePass(&ID) {
      if (TargetArch == "x86-64") {
        arch_size_ = 64;
      } else {
        arch_size_ = 32;
      }
    }

    // instruction_address = function_address + c_offset
    // (number of mops is always less or equal to the function size)
    Constant *getInstructionAddr(int mop_index,
                                 BasicBlock::iterator cur_inst) {
      Value *cur_fun = cur_inst->getParent()->getParent();
      Constant *c_offset = ConstantInt::get(PlatformInt, mop_index);
      Constant *result =
          ConstantExpr::getAdd(
              ConstantExpr::getPtrToInt(cast<Constant>(cur_fun), PlatformInt),
              c_offset);
      if (cur_inst) {
        DumpInstructionDebugInfo(result, cur_inst);
      }
      return result;
    }

    // Get the target architecture word size in bits.
    int getArchSize() {
      return arch_size_;
    }

    uintptr_t getModuleID(Module &M) {
      uintptr_t result = 0;
      char tmp;
      std::string name = M.getModuleIdentifier();
      for (size_t i = 0; i < name.size(); i++) {
        tmp = name[i];
        result = (result ^ tmp) % kFNV1aModulo;
        result = (result * kFNV1aPrime) % kFNV1aModulo;
      }
      return result;
    }

    bool isDtor(llvm::StringRef mangled_name) {
      int status;
      char *demangled = NULL;
#if defined(__GNUC__)
      demangled = __cxxabiv1::__cxa_demangle(mangled_name.data(),
                                             0, 0, &status);
#endif
      if (demangled) {
        char *found = strchr(demangled, '~');
        free(demangled);
        if (found) {
          return true;
        } else {
          return false;
        }
      } else {
        // TODO(glider): need a demangler here.
        if (mangled_name.find("D0") != std::string::npos) return true;
        if (mangled_name.find("D1") != std::string::npos) return true;
      }
      return false;
    }


    void writeDebugInfo(Module &M) {
      // The debug info is stored in a per-module global structure named
      // "rtl_debug_info${ModuleID}".
      // TODO(glider): this may lead to name collisions.
      LLVMContext &Context = M.getContext();
      std::vector<Constant*> dummy;
      dummy.push_back(ConstantInt::get(PlatformInt, ModuleID));
      map<string, size_t> files;
      map<string, size_t> paths;
      map<string, size_t> symbols;
      uintptr_t files_size = 0, files_count = 0;
      uintptr_t paths_size = 0, paths_count = 0;
      uintptr_t symbols_size = 0, symbols_count = 0;
      uintptr_t pcs_size = 0;
      vector<Constant*> files_raw;
      for (set<string>::iterator it = debug_file_set.begin();
           it != debug_file_set.end();
           ++it) {
        files[*it] = files_count;
        files_count++;
        files_size += (it->size() + 1);
        for (int i = 0; i < it->size(); i++) {
          files_raw.push_back(ConstantInt::get(Int8, it->c_str()[i]));
        }
        files_raw.push_back(ConstantInt::get(Int8, 0));
      }

      vector<Constant*> paths_raw;
      for (set<string>::iterator it = debug_path_set.begin();
           it != debug_path_set.end();
           ++it) {
        paths[*it] = paths_count;
        paths_count++;
        paths_size += (it->size() + 1);
        for (int i = 0; i < it->size(); i++) {
          paths_raw.push_back(ConstantInt::get(Int8, it->c_str()[i]));
        }
        paths_raw.push_back(ConstantInt::get(Int8, 0));
      }

      vector<Constant*> symbols_raw;
      for (set<string>::iterator it = debug_symbol_set.begin();
           it != debug_symbol_set.end();
           ++it) {
        symbols[*it] = symbols_count;
        symbols_count++;
        symbols_size += (it->size() + 1);
        for (int i = 0; i < it->size(); i++) {
          symbols_raw.push_back(ConstantInt::get(Int8, it->c_str()[i]));
        }
        symbols_raw.push_back(ConstantInt::get(Int8, 0));
      }
      vector<Constant*> pcs;

      // pc, symbol, path, file, line
      StructType *PcInfo = StructType::get(Context,
                                           PlatformInt, PlatformInt,
                                           PlatformInt,
                                           PlatformInt, PlatformInt,
                                           NULL);
      for (map<Constant*, DebugPcInfo>::iterator it = debug_pc_map.begin();
           it != debug_pc_map.end();
           ++it) {
        vector<Constant*> pc;
        ///pc.push_back(ConstantInt::get(PlatformInt, it->first));
        pc.push_back(it->first);
        pc.push_back(ConstantInt::get(PlatformInt, symbols[it->second.symbol]));
        pc.push_back(ConstantInt::get(PlatformInt, paths[it->second.path]));
        pc.push_back(ConstantInt::get(PlatformInt, files[it->second.file]));
        pc.push_back(ConstantInt::get(PlatformInt, it->second.line));
        pcs.push_back(ConstantStruct::get(PcInfo, pc));
        pcs_size++;
      }
      // magic,
      // paths_size, files_size, symbols_size, pcs_size,
      // paths[], files[], symbols[], pcs[]
      StructType *DebugInfoType = StructType::get(Context,
          Int32,
          PlatformInt, PlatformInt, PlatformInt, PlatformInt,
          ArrayType::get(Int8, paths_size),
          ArrayType::get(Int8, files_size),
          ArrayType::get(Int8, symbols_size),
          ArrayType::get(PcInfo, pcs_size),
          NULL);

      vector<Constant*> debug_info;
      debug_info.push_back(ConstantInt::get(Int32, kDebugInfoMagicNumber));
      debug_info.push_back(ConstantInt::get(PlatformInt, paths_size));
      debug_info.push_back(ConstantInt::get(PlatformInt, files_size));
      debug_info.push_back(ConstantInt::get(PlatformInt, symbols_size));
      debug_info.push_back(ConstantInt::get(PlatformInt, pcs_size));

      debug_info.push_back(
          ConstantArray::get(ArrayType::get(Int8, paths_size), paths_raw));
      debug_info.push_back(
          ConstantArray::get(ArrayType::get(Int8, files_size), files_raw));
      debug_info.push_back(
          ConstantArray::get(ArrayType::get(Int8, symbols_size), symbols_raw));
      debug_info.push_back(
          ConstantArray::get(ArrayType::get(PcInfo, pcs_size), pcs));
      Constant *DebugInfo = ConstantStruct::get(DebugInfoType, debug_info);

      char var_id_str[50];
      snprintf(var_id_str, sizeof(var_id_str), "rtl_debug_info%d", ModuleID);

      GlobalValue *GV = new GlobalVariable(
          M,
          DebugInfoType,
          /*isConstant*/true,
          GlobalValue::InternalLinkage,
          DebugInfo,
          var_id_str,
          /*ThreadLocal*/false,
          /*AddressSpace*/0
      );
      GV->setSection("tsan_rtl_debug_info");
    }

    BlockSet &getPredecessors(BasicBlock* bb) {
      return predecessors[bb];
    }

    // In fact a BB ends with a call iff it contains a call.
    bool endsWithCall(Function::iterator BB) {
      for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
           BI != BE;
           ++BI) {
        if (isaCallOrInvoke(BI)) return true;
      }
      return false;
    }

    bool traceHasCycles(Trace &trace,
                        BasicBlock *current_bb, BlockSet &used) {
      TerminatorInst *BBTerm = current_bb->getTerminator();
      // Iterate over the successors of the block being considered.
      // If it can be added to the trace, do it.
      for (int i = 0, e = BBTerm->getNumSuccessors(); i != e; ++i) {
        BasicBlock *child = BBTerm->getSuccessor(i);
        // Skip the children not belonging to the same trace.
        // If the child doesn't belong to the same trace, add current_bb to the
        // list of trace exits.
        if (trace.blocks.find(child) == trace.blocks.end()) {
          trace.exits.insert(current_bb);
        }
        if (used.find(child) != used.end()) return true;
        used.insert(child);
        if (traceHasCycles(trace, child, used)) return true;
        used.erase(child);
      }
      return false;
    }

    // A trace is valid if and only if:
    //  -- it has a single entry point
    //  -- it contains no loops
    // TODO(glider): may want to handle indirectbr.
    bool validTrace(Trace &trace) {
      BlockVector to_see;
      BlockSet entries;
      BlockSet accessible;
      int num_edges = 0;
#if DEBUG > 1
        errs() << "VALIDATING:\n";
        for (BlockSet::iterator I = trace.begin(), E = trace.end();
             I != E; ++I) {
          errs() << "    " << (*I)->getName();
        }
        errs() << "\n";
#endif

      // Initialize to_see and find all the entry points.
      for (BlockSet::iterator BB = trace.blocks.begin(), E = trace.blocks.end();
           BB != E; ++BB) {
        to_see.push_back(*BB);
        BlockSet &pred = getPredecessors(*BB);
        Function *F = (*BB)->getParent();
        BasicBlock *entry = F->begin();
#if DEBUG > 1
        errs() << "!!" << (*BB)->getName() << "\n";
#endif
        if (*BB == entry) {
          entries.insert(*BB);
#if DEBUG > 1
          errs() << "? ->" << (*BB)->getName() << "\n";
#endif
        }
        for (BlockSet::iterator I = pred.begin(), E = pred.end();
             I != E; ++I) {
#if DEBUG > 1
          errs() << (*I)->getName() << "->" << (*BB)->getName() << "\n";
#endif
          if (trace.blocks.find(*I) != trace.blocks.end()) {
            num_edges++;
          } else {
            entries.insert(*BB);
          }
        }
      }
      if (entries.size() > 1) {
#if DEBUG > 1
        errs() << "MULTIPLE ENTRY DETECTED\n";
#endif
        return false;
      }

      trace.exits.clear();
      for (BlockSet::iterator BB = trace.blocks.begin(), E = trace.blocks.end();
           BB != E; ++BB) {
        TerminatorInst *BBTerm = (*BB)->getTerminator();
        // Iff any successor of a basic block doesn't belong to a trace,
        // then no successors of that basic block should belong to the trace.
        if (BBTerm->getNumSuccessors() > 0) {
          BasicBlock *child = BBTerm->getSuccessor(0);
          bool has_ext_successors =
              (trace.blocks.find(child) == trace.blocks.end());
          if (has_ext_successors) {
            trace.exits.insert(*BB);
          }
          for (int i = 0, e = BBTerm->getNumSuccessors(); i != e; ++i) {
            BasicBlock *child = BBTerm->getSuccessor(i);
            // If any basic block loops to the trace entry, the trace is
            // invalid. It's guaranteed that no basic block loops to itself.
            if (child == trace.entry) return false;
            bool is_external = trace.blocks.find(child) == trace.blocks.end();
            if (has_ext_successors != is_external) {
              return false;
            }
          }
        }
        // Treat the UNREACHABLE instruction as a trace exit. This won't hurt
        // because its instrumentation will be unreachable too.
        if (isa<UnreachableInst>(BBTerm) || isa<ReturnInst>(BBTerm)) {
          trace.exits.insert(*BB);
        }
      }
      // A valid trace should have at least one exit!
      assert(trace.exits.size());
      return true;
      // TODO(glider): looks like the greedy algorithm allows us not to check
      // for cycles inside trace. But we may want to assert that the only
      // possible cycle can lead to the entry block from within the trace.
      BlockSet used_empty;
      // entries.begin() is the only trace entry.
      if (traceHasCycles(trace, *(entries.begin()), used_empty)) {
#if DEBUG > 1
        errs() << "LOOP DETECTED\n";
#endif
        return false;
      }
      return true;
    }

    // Build a valid trace starting at |Entry| and containing no basic blocks
    // from |used| and write it into |trace|.
    bool buildClosure(Function::iterator Entry,
                      Trace &trace, BlockSet &used) {
      BlockVector to_see;
      to_see.push_back(Entry);
      trace.blocks.insert(Entry);
      used.insert(Entry);
      for (int i = 0; i<to_see.size(); ++i) {
        BasicBlock *current_bb = to_see[i];
        // A call should end the trace immediately.
        if (endsWithCall(current_bb)) continue;
        TerminatorInst *BBTerm = current_bb->getTerminator();
        // Iterate over the successors of the block being considered.
        // If it can be added to the trace, do it.
        for (int i = 0, e = BBTerm->getNumSuccessors(); i != e; ++i) {
          BasicBlock *child = BBTerm->getSuccessor(i);
          if (trace.blocks.find(child) != trace.blocks.end()) continue;
          if (used.find(child) != used.end()) continue;
          Instruction *First = child->begin();
          if (isa<CallInst>(First) || isa<InvokeInst>(First)) continue;
          trace.blocks.insert(child);
          if (validTrace(trace)) {
            assert(trace.exits.size());
            used.insert(child);
            to_see.push_back(child);
          } else {
            trace.blocks.erase(child);
          }
        }
      }
      // Populate the vector of trace exits.
      trace.exits.clear();
      for (BlockSet::iterator BB = trace.blocks.begin(), E = trace.blocks.end();
           BB != E; ++BB) {
        TerminatorInst *BBTerm = (*BB)->getTerminator();
        if (BBTerm->getNumSuccessors() > 0)  {
          // We've already checked that having an external successor implies
          // being an exit.
          BasicBlock *child = BBTerm->getSuccessor(0);
          bool has_ext_successors =
              (trace.blocks.find(child) == trace.blocks.end());
          if (has_ext_successors) {
            trace.exits.insert(*BB);
          }
        }
        if (endsWithCall(*BB)) trace.exits.insert(*BB);
        if (isa<ReturnInst>(BBTerm) || isa<UnreachableInst>(BBTerm)) {
          trace.exits.insert(*BB);
        }
      }
      assert(trace.exits.size());
    }

    void cachePredecessors(Function &F) {
      for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
        TerminatorInst *BBTerm = BB->getTerminator();
        for (int i = 0, e = BBTerm->getNumSuccessors(); i != e; ++i) {
          BasicBlock *child = BBTerm->getSuccessor(i);
          predecessors[child].insert(BB);
        }
      }
    }

    TraceVector buildTraces(Function &F) {
      TraceVector traces;
      BlockSet used_bbs;
      BlockVector to_see;
      BlockSet visited;
      cachePredecessors(F);
      Trace current_trace;
      to_see.push_back(F.begin());
      for (int i = 0; i < to_see.size(); ++i) {
        BasicBlock *current_bb = to_see[i];
#if DEBUG > 1
        errs() << "BB: " << current_bb->getName() << "\n";
#endif
        if (used_bbs.find(current_bb) == used_bbs.end()) {
          assert(current_trace.blocks.size() == 0);
          current_trace.entry = current_bb;
          buildClosure(current_bb, current_trace, used_bbs);
          assert(current_trace.exits.size());
          traces.push_back(current_trace);
          current_trace.blocks.clear();
          current_trace.entry = NULL;
        }
        TerminatorInst *BBTerm = current_bb->getTerminator();
        for (int ch = 0, e = BBTerm->getNumSuccessors(); ch != e; ++ch) {
          BasicBlock *child = BBTerm->getSuccessor(ch);
          if (visited.find(child) == visited.end()) {
            to_see.push_back(child);
            visited.insert(child);
          }
        }
      }
      return traces;
    }

    // Insert the code that updates the shadow stack with the value of |addr|.
    // This effectively adds the following instructions:
    //
    //   %0 = load i64** getelementptr inbounds (%struct.CallStackPod* @ShadowStack, i64 0, i32 0), align 32
    //   store i64 %pc, i64* %0, align 8
    //   %1 = load i64** getelementptr inbounds (%struct.CallStackPod* @ShadowStack, i64 0, i32 0), align 32
    //   %2 = getelementptr inbounds i64* %1, i64 1
    //   store i64* %2, i64** getelementptr inbounds (%struct.CallStackPod* @ShadowStack, i64 0, i32 0), align 32
    //
    // Or, in C++:
    //
    //   *ShadowStack.end_ = (uintptr_t)addr;
    //   ShadowStack.end_++;
    //
    void InsertRtnCall(Constant *addr, BasicBlock::iterator &Before) {
#if DEBUG
        std::vector<Value*> inst(1);
        inst[0] = addr;
        CallInst::Create(RtnCallFn, inst.begin(), inst.end(), "", Before);
#else
      // TODO(glider): each call instruction should update the top of the shadow
      // stack.
      // Is this necessary if we already update the stacks inside each wrapper?
      std::vector <Value*> end_idx;
      end_idx.push_back(ConstantInt::get(PlatformInt, 0));
      end_idx.push_back(ConstantInt::get(Int32, 0));
      Value *StackEndPtr =
          GetElementPtrInst::Create(ShadowStack,
                                    end_idx.begin(), end_idx.end(),
                                    "", Before);
      Value *StackEnd = new LoadInst(StackEndPtr, "", Before);
      CurrentStackEnd = StackEnd;
      new StoreInst(addr, StackEnd, Before);

      std::vector <Value*> new_idx;
      new_idx.push_back(ConstantInt::get(Int32, 1));
      Value *NewStackEnd =
          GetElementPtrInst::Create(StackEnd,
                                    new_idx.begin(), new_idx.end(),
                                    "", Before);
      new StoreInst(NewStackEnd, StackEndPtr, Before);
#endif
    }

    // Insert the code that pops a stack frame from the shadow stack.
    void InsertRtnExit(BasicBlock::iterator &Before) {
#if DEBUG
      std::vector<Value*> inst(0);
      CallInst::Create(RtnExitFn, inst.begin(), inst.end(), "", Before);
#else
      std::vector <Value*> end_idx;
      end_idx.push_back(ConstantInt::get(PlatformInt, 0));
      end_idx.push_back(ConstantInt::get(Int32, 0));
      Value *StackEndPtr =
          GetElementPtrInst::Create(ShadowStack,
                                    end_idx.begin(), end_idx.end(),
                                    "", Before);
      // TODO(glider): the following lines may introduce an error if no
      // dependence analysis is done after the instrumentation.
///      Value *StackSize = new LoadInst(StackSizePtr, "", Before);
///      Value *NewSize = BinaryOperator::Create(Instruction::Sub,
///                                              CurrentStackSize,
///                                              ConstantInt::get(PlatformInt, 1),
///                                              "", Before);
      // Restore the original shadow stack |end_| pointer.
      assert(CurrentStackEnd);
      new StoreInst(CurrentStackEnd, StackEndPtr, Before);
#endif
    }

    virtual bool runOnModule(Module &M) {
      TraceCount = 0;
      InstrumentedTraceCount = 0;
      ModuleFunctionCount = 0;
      ModuleMopCount = 0;
      ModuleID = getModuleID(M);
      LLVMContext &Context = M.getContext();

      AA = &getAnalysis<AliasAnalysis>();

      // Arch size dependent types.
      if (getArchSize() == 64) {
        UIntPtr = Type::getInt64PtrTy(Context);
        PlatformInt = Type::getInt64Ty(Context);
        ArithmeticPtr = Type::getInt64Ty(Context);
      } else {
        UIntPtr = Type::getInt32PtrTy(Context);
        PlatformInt = Type::getInt32Ty(Context);
        ArithmeticPtr = Type::getInt32Ty(Context);
      }

      Int8 = Type::getInt8Ty(Context);
      Int8Ptr = Type::getInt8PtrTy(Context);
      Int32 = Type::getInt32Ty(Context);
      Void = Type::getVoidTy(Context);

      // MopType represents the following class declared in ts_trace_info.h:
      // struct MopInfo {
      //   uintptr_t pc;
      //   uint32_t  size;
      //   bool      is_write;
      // };
      MopType = StructType::get(Context, PlatformInt, Int32, Int8, NULL);

      MopArrayType = ArrayType::get(MopType, kTLEBSize);
      LiteRaceCountersArrayType = ArrayType::get(Int32, kLiteRaceNumTids);
      LiteRaceSkipArrayType = ArrayType::get(Int32, kLiteRaceNumTids);

      // struct LiteRaceCounters {
      //   uint32_t counter;
      //   int32_t num_to_skip;
      // };
      LiteRaceCountersType = StructType::get(Context,
                                             Int32, Int32, NULL);
      LiteRaceStorageLineType =
          ArrayType::get(LiteRaceCountersType,
                         kLiteRaceStorageSize);
      LiteRaceStorageType =
          ArrayType::get(LiteRaceStorageLineType,
                         kLiteRaceNumTids);

      LiteRaceStoragePtrType = PointerType::get(LiteRaceStorageType, 0);

      // TraceInfoType represents the following class declared in
      // ts_trace_info.h:
      // struct TraceInfoPOD {
      //   enum { kLiteRaceNumTids = 8 };  // takes zero bytes
      //   size_t n_mops_;
      //   size_t pc_;
      //   size_t counter_;
      //   uint32_t literace_counters[kLiteRaceNumTids];
      //   int32_t literace_num_to_skip[kLiteRaceNumTids];
      //   MopInfo mops_[1];
      // };
      TraceInfoType = StructType::get(Context,
                                    PlatformInt, PlatformInt,
                                    PlatformInt,
                                    MopArrayType,
                                    LiteRaceCountersArrayType,
                                    LiteRaceSkipArrayType,
                                    NULL);
      TraceInfoTypePtr = PointerType::get(TraceInfoType, 0);

      // CallStackType represents the following class declared in
      // thread_sanitizer.h:
      //
      // const size_t kMaxCallStackSize = 1 << 12;
      // struct CallStackPod {
      //   uintptr_t *end_;
      //   uintptr_t pcs_[kMaxCallStackSize];
      // };
      CallStackArrayType = ArrayType::get(PlatformInt, kMaxCallStackSize);
      CallStackType = StructType::get(Context,
                                      UIntPtr,
                                      CallStackArrayType,
                                      NULL);
      ShadowStack = new GlobalVariable(M,
                                       CallStackType,
                                       /*isConstant*/true,
                                       GlobalValue::ExternalWeakLinkage,
                                       /*Initializer*/0,
                                       "ShadowStack",
                                       /*InsertBefore*/0,
                                       /*ThreadLocal*/true);
      LiteraceTid = new GlobalVariable(M,
                                       PlatformInt,
                                       /*isConstant*/false,
                                       GlobalValue::ExternalWeakLinkage,
                                       /*Initializer*/0,
                                       "LTID",
                                       /*InsertBefore*/0,
                                       /*ThreadLocal*/true);
      TLEBTy = ArrayType::get(UIntPtr, kTLEBSize);
      TLEBPtrType = PointerType::get(UIntPtr, 0);
      TLEB = new GlobalVariable(M,
                                TLEBTy,
                                /*isConstant*/true,
                                GlobalValue::ExternalWeakLinkage,
                                /*Initializer*/0,
                                "TLEB",
                                /*InsertBefore*/0,
                                /*ThreadLocal*/true);

      // void* bb_flush(next_mops)
      // TODO(glider): need another name, because we now flush superblocks, not
      // basic blocks.
      BBFlushFn = M.getOrInsertFunction("bb_flush",
                                        Void,
                                        TraceInfoTypePtr, (Type*)0);
      cast<Function>(BBFlushFn)->setLinkage(Function::ExternalWeakLinkage);

      // void* bb_flush_current(cur_mops)
      // TODO(glider): need another name, because we now flush superblocks, not
      // basic blocks.
      BBFlushCurrentFn = M.getOrInsertFunction("bb_flush_current",
                                               Void,
                                               TraceInfoTypePtr, (Type*)0);
      cast<Function>(BBFlushCurrentFn)->
          setLinkage(Function::ExternalWeakLinkage);


      // void rtn_call(void *addr)
      // TODO(glider): we should finally get rid of it at all.
      RtnCallFn = M.getOrInsertFunction("rtn_call",
                                        Void,
                                        PlatformInt, (Type*)0);

      // void rtn_exit()
      // TODO(glider): we should finally get rid of it at all.
      RtnExitFn = M.getOrInsertFunction("rtn_exit",
                                        Void, (Type*)0);

      MemCpyFn = M.getOrInsertFunction("rtl_memcpy",
                                       UIntPtr,
                                       UIntPtr, UIntPtr, PlatformInt, (Type*)0);
      cast<Function>(MemCpyFn)->setLinkage(Function::ExternalWeakLinkage);
      MemMoveFn = M.getOrInsertFunction("rtl_memmove",
                                       UIntPtr,
                                       UIntPtr, UIntPtr, PlatformInt, (Type*)0);
      cast<Function>(MemMoveFn)->setLinkage(Function::ExternalWeakLinkage);
      const Type *Tys[] = { PlatformInt };
      MemSetIntrinsicFn = Intrinsic::getDeclaration(&M,
                                                    Intrinsic::memset,
                                                    Tys, /*numTys*/1);

      // Split each basic block into smaller blocks containing no more than one
      // call instruction at the end.
      for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
        for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
          bool need_split = false;
          BasicBlock::iterator OldBI = BB->end();
          for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
               BI != BE;
               ++BI) {
            if (isa<LoadInst>(BI) || isa<StoreInst>(BI)) {
              need_split = true;
            }
            if (isa<TerminatorInst>(BI)) {
              for (int i = 0,
                       e = cast<TerminatorInst>(BI)->getNumSuccessors();
                   i != e; ++i) {
                BasicBlock *child = cast<TerminatorInst>(BI)->getSuccessor(i);
                if (child == BB) {
                  SplitBlock(BB, BI, this);
                  break;
                }
              }
              break;
            }
            // A call may not occur inside of a basic block, iff this is not a
            // call to @llvm.dbg.declare
            if (isaCallOrInvoke(BI)) {
              if (need_split) {
                SplitBlock(BB, BI, this);
                need_split = false;
                break;
              }
            }
            if (isa<MemTransferInst>(BI)) {
              InstrumentMemTransfer(BI);
              if (need_split) {
                SplitBlock(BB, BI, this);
                need_split = false;
                break;
              }
            }
          }
        }
      }

      for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
        ModuleFunctionCount++;
        FunctionMopCount = 0;
        int startTraceCount = TraceCount;
        bool first_dtor_bb = false;

        if (F->isDeclaration()) continue;

        // TODO(glider): document this.
        // I even can't remember why in the world we do skip new/delete.
        // Probably should be removed.
        if ((F->getName()).find("_Znw") != std::string::npos) {
          continue;
        }
        if ((F->getName()).find("_Zdl") != std::string::npos) {
          continue;
        }
        if (isDtor(F->getName())) first_dtor_bb = WorkaroundVptrRace;

        instrumentation_stats.newFunction();
        instrumentation_stats.newBasicBlocks(F->size());
        // Instrument the traces.
        TraceVector traces(buildTraces(*F));
        for (int i = 0; i < traces.size(); ++i) {
          assert(traces[i].exits.size());
          runOnTrace(M, traces[i], first_dtor_bb);
          first_dtor_bb = false;
        }

        // Instrument routine calls and exits.
        // InsertRtnExit uses the shadow stack size obtained by InsertRtnCall
        // and should be always executed after it.
        BasicBlock::iterator First = F->begin()->begin();
        InsertRtnCall(getInstructionAddr(0, First), First);
        for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
          for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
               BI != BE;
               ++BI) {
            if (isa<ReturnInst>(BI)) {
              InsertRtnExit(BI);
            }
          }
        }
      }
      writeDebugInfo(M);
      if (PrintStats) instrumentation_stats.printStats();
      return true;
    }

    void insertFlushCall(Trace &trace, Instruction *Before) {
      assert(Before);
      if (!EnableLiteraceSampling) {
        // Sampling is off -- just insert an unconditional call to bb_flush().
        std::vector <Value*> Args(1);
        std::vector <Value*> idx;
        idx.push_back(ConstantInt::get(PlatformInt, 0));
        Value *PassportPtr =
            GetElementPtrInst::Create(TracePassportGlob,
                                      idx.begin(),
                                      idx.end(),
                                      "",
                                      Before);
        Args[0] = BitCastInst::CreatePointerCast(PassportPtr,
                                                 TraceInfoTypePtr,
                                                 "",
                                                 Before);
        CallInst::Create(BBFlushFn, Args.begin(), Args.end(), "", Before);
      } else {
      }
    }

    void insertFlushCurrentCall(Trace &trace, Instruction *Before) {
      assert(Before);
      if (!EnableLiteraceSampling) {
        // Sampling is off -- just insert an unconditional call to bb_flush().
        std::vector <Value*> Args(1);
        std::vector <Value*> idx;
        idx.push_back(ConstantInt::get(PlatformInt, 0));
        Value *PassportPtr =
            GetElementPtrInst::Create(TracePassportGlob,
                                      idx.begin(),
                                      idx.end(),
                                      "",
                                      Before);
        Args[0] = BitCastInst::CreatePointerCast(PassportPtr, TraceInfoTypePtr,
                                                 "",
                                                 Before);
        CallInst::Create(BBFlushCurrentFn,
                         Args.begin(), Args.end(), "", Before);

      } else {
        // Sampling is on -- each trace should be instrumented with the code
        // that decrements the literace counter and checks whether it is
        // greater than 0. If yes, the TLEB should be flushed, otherwise it
        // should be cleared. If we don't clean up the TLEB, the dirty
        // addresses may be occasionally passed to ThreadSanitizer during the
        // next flush.
        //
        //       literace > 0 ?
        //          /    \
        //         /      \
        //        /        \
        //       /          \
        //   yes, flush  no, cleanup
        //       \          /
        //        \        /
        //         \      /
        //           exit
        //
        //     int32_t num_to_skip = --(literace_num_to_skip[tid_modulo_num]);
        //     if (num_to_skip > 0) {
        //       return true;
        //     }
        //     return false;
        //
        //   %0 = load i32* @tid_modulo_num, align 4
        //   %1 = sext i32 %0 to i64
        //   %2 = getelementptr inbounds [7 x i32]* @literace_num_to_skip, i64 0, i64 %1
        //   %3 = load i32* %2, align 4
        //   %4 = sub i32 %3, 1
        //   store i32 %4, i32* %2, align 4
        //   %5 = icmp sgt i32 %4, 0

        BasicBlock *BB = Before->getParent();
        BasicBlock *FinishBB = SplitBlock(BB, Before, this);
        TerminatorInst *BBOldTerm = BB->getTerminator();
        LLVMContext &Context = BB->getContext();
        BasicBlock *CleanupBB =
            BasicBlock::Create(Context, "clean_tleb", BB->getParent());
        BasicBlock *FlushBB =
            BasicBlock::Create(Context, "flush_tleb", BB->getParent());
        // TODO(glider): implement the flush.
        vector <Value*> num_to_skip;
        num_to_skip.push_back(ConstantInt::get(Int32, 0));
        Value *LiteraceTidValue = new LoadInst(LiteraceTid, "", BBOldTerm);
        num_to_skip.push_back(LiteraceTidValue);
        num_to_skip.push_back(ConstantInt::get(
            Int32, (InstrumentedTraceCount-1) % kLiteRaceStorageSize));
        num_to_skip.push_back(ConstantInt::get(Int32, 1));

        Value *NumToSkipSlot =
            GetElementPtrInst::Create(LiteRaceStorageGlob,
                                      num_to_skip.begin(),
                                      num_to_skip.end(),
                                      "",
                                      BBOldTerm);

        Value *NumToSkip = new LoadInst(NumToSkipSlot, "", BBOldTerm);
        Value *NewNumToSkip =
            BinaryOperator::CreateSub(NumToSkip,
                                      ConstantInt::get(Int32, 1),
                                      "",
                                      BBOldTerm);

        new StoreInst(NewNumToSkip, NumToSkipSlot, BBOldTerm);
        Value *LiteraceCond = new ICmpInst(BBOldTerm,
                                           ICmpInst::ICMP_SGT,
                                           NewNumToSkip,
                                           ConstantInt::get(Int32, 0),
                                           "");
        BranchInst *BBNewTerm = BranchInst::Create(/*ifTrue*/CleanupBB,
                                                   /*ifFalse*/FlushBB,
                                                   LiteraceCond);
        ReplaceInstWithInst(BBOldTerm, BBNewTerm);

        // Set up the cleanup block. It should contain:
        //  memset(TLEB, 0, num_mops)
        //  branch to the original block end
        BranchInst *CleanupTerm = BranchInst::Create(FinishBB, CleanupBB);
        vector <Value*> MSArgs;
        MSArgs.push_back(BitCastInst::CreatePointerCast(TLEB,
                                                        Int8Ptr,
                                                        "",
                                                        CleanupTerm));
        MSArgs.push_back(ConstantInt::get(Int8, 0));
        MSArgs.push_back(ConstantInt::get(PlatformInt,
                                          trace.num_mops * arch_size_ / 8));
        MSArgs.push_back(ConstantInt::get(Int32, 1));
        CallInst::Create(MemSetIntrinsicFn,
                         MSArgs.begin(), MSArgs.end(),
                         "", CleanupTerm);

        BranchInst *FlushTerm = BranchInst::Create(FinishBB, FlushBB);
        std::vector <Value*> Args(1);
        std::vector <Value*> idx;
        idx.push_back(ConstantInt::get(PlatformInt, 0));
        Args[0] =
            GetElementPtrInst::Create(TracePassportGlob,
                                      idx.begin(),
                                      idx.end(),
                                      "",
                                      FlushTerm);
        Args[0] = BitCastInst::CreatePointerCast(Args[0],
                                                 TraceInfoTypePtr,
                                                 "",
                                                 FlushTerm);
        CallInst::Create(BBFlushCurrentFn,
                         Args.begin(), Args.end(),
                         "", FlushTerm);
      }
    }

    void runOnTrace(Module &M, Trace &trace, bool first_dtor_bb) {
      TLEBIndex = 0;
      TraceCount++;
      instrumentation_stats.newTrace();
      markMopsToInstrument(M, trace);
      bool have_passport = makeTracePassport(M, trace);
      if (have_passport) {
        for (BlockSet::iterator TI = trace.blocks.begin(),
                                TE = trace.blocks.end();
             TI != TE; ++TI) {
          runOnBasicBlock(M, *TI, first_dtor_bb, trace);
          first_dtor_bb = false;
        }
        // If a trace has a passport, we should be able to insert a flush call
        // before its exit points.
        assert(trace.exits.size());
        for (BlockSet::iterator EI = trace.exits.begin(),
                                EE = trace.exits.end();
             EI != EE; ++EI) {

          insertFlushCurrentCall(trace, (*EI)->getTerminator());
        }
      }
    }

    void DumpInstructionDebugInfo(Constant *addr, BasicBlock::iterator BI) {
      DILocation Loc(BI->getMetadata("dbg"));
      BasicBlock::iterator OldBI = BI;
      if (!Loc.getLineNumber()) {
        for (BasicBlock::iterator BE = BI->getParent()->end();
             BI != BE; ++BI) {
            Loc = DILocation(BI->getMetadata("dbg"));
            if (Loc.getLineNumber()) break;
        }
      }
      if (!Loc.getLineNumber()) {
        BI = OldBI;
        Loc = DILocation(OldBI->getMetadata("dbg"));
      }
      std::string file = Loc.getFilename();
      std::string dir = Loc.getDirectory();
      uintptr_t line = Loc.getLineNumber();

      debug_path_set.insert(dir);
      debug_file_set.insert(file);
      debug_symbol_set.insert(BI->getParent()->getParent()->getName());
      debug_pc_map.insert(
          make_pair(addr, DebugPcInfo(BI->getParent()->getParent()->getName(),
                                      dir, file, line)));
    }


    bool isaCallOrInvoke(BasicBlock::iterator &BI) {
      return ((isa<CallInst>(BI) && (!isa<DbgDeclareInst>(BI))) ||
              isa<InvokeInst>(BI));
    }

    int getMopPtrSize(Value *mopPtr, bool isStore) {
      int result = getArchSize();
      const Type *mop_type = mopPtr->getType();
      if (mop_type->isSized()) {
        if (cast<PointerType>(mop_type)->getElementType()->isSized()) {
          result = getAnalysis<TargetData>().getTypeStoreSizeInBits(
              cast<PointerType>(mop_type)->getElementType());
        }
      }
      return result;
    }

    bool markMopsToInstrument(Module &M, Trace &trace) {
      bool isStore, isMop;
      int size;
      // Map from AA location into access size.
      typedef map<pair<Value*, int>, Instruction*> LocMap;
      LocMap store_map, load_map;
      for (BlockSet::iterator TI = trace.blocks.begin(),
                              TE = trace.blocks.end();
           TI != TE; ++TI) {
        store_map.clear();
        load_map.clear();
        for (BasicBlock::iterator BI = (*TI)->begin(), BE = (*TI)->end();
             BI != BE; ++BI) {
          isMop = false;
          if (isa<LoadInst>(BI)) {
            isStore = false;
            isMop = true;
          }
          if (isa<StoreInst>(BI)) {
            isStore = true;
            isMop = true;
          }
          if (isaCallOrInvoke(BI)) {
            // CallInst or maybe other instructions that may cause a jump.
            // TODO(glider): assert that the next instruction is strictly a
            // branch.
            //assert(false);
          }
          if (isMop) {
            if (InstrumentAll) {
              trace.to_instrument.insert(BI);
              continue;
            }
            // Falling through to the alias-analysis-based optimization.
            // If two operations in the same trace access the same memory
            // location, then we can instrument only one of them (the latter
            // among the strongest):
            //
            // L1, L2 -> instrument L2
            // L1, S2 -> instrument S2
            // S1, L2 -> instrument S1
            // S1, S2 -> instrument S2
            llvm::Instruction &IN = *BI;
            Value *MopPtr;
            if (isStore) {
              MopPtr = (static_cast<StoreInst&>(IN).getPointerOperand());
            } else {
              MopPtr = (static_cast<LoadInst&>(IN).getPointerOperand());
            }
            size = getMopPtrSize(MopPtr, isStore);

            bool has_alias = false;
            // Iff the current operation is STORE, it may modify store_map.
            if (isStore) {
              for (LocMap::iterator LI = store_map.begin(),
                                    LE = store_map.end();
                   LI != LE; ++LI) {
                const pair<Value*, int> &location = LI->first;
                AliasAnalysis::AliasResult R = AA->alias(MopPtr, size,
                                                         location.first,
                                                         location.second);
                if ((R == AliasAnalysis::MustAlias) &&
                    (size == location.second)) {
                  // We've already seen a STORE of the same size accessing the
                  // same memory location. We're a STORE, too, so drop it.
                  trace.to_instrument.erase(LI->second);
                  trace.to_instrument.insert(BI);
                  store_map.erase(LI);
                  store_map[make_pair(MopPtr, size)] = BI;
                  has_alias = true;
                  // There cannot be other STORE operations aliasing the
                  // same location.
                  break;
                }
              }
              // Drop the possible LOAD accessing the same memory location.
              for (LocMap::iterator LI = load_map.begin(),
                                    LE = load_map.end();
                   LI != LE; ++LI) {
                const pair<Value*, int> &location = LI->first;
                AliasAnalysis::AliasResult R = AA->alias(MopPtr, size,
                                                         location.first,
                                                         location.second);
                if ((R == AliasAnalysis::MustAlias) &&
                    (size == location.second)) {
                  trace.to_instrument.erase(LI->second);
                  // There cannot be other LOAD operations aliasing the
                  // same location.
                  break;
                }
              }
              if (!has_alias) {
                store_map[make_pair(MopPtr, size)] = BI;
                trace.to_instrument.insert(BI);
              }
            }
            // Any newer access to the same memory location removes the
            // previous access from the load_map.
            for (LocMap::iterator LI = load_map.begin(), LE = load_map.end();
                 LI != LE; ++LI) {
              const pair<Value*, int> &location = LI->first;
              AliasAnalysis::AliasResult R = AA->alias(MopPtr, size,
                                                       location.first,
                                                       location.second);
              if ((R == AliasAnalysis::MustAlias) &&
                  (size == location.second)) {
                // Drop the previous access.
                trace.to_instrument.erase(LI->second);
                // It's ok to insert the same BI into to_instrument twice.
                trace.to_instrument.insert(BI);
                load_map.erase(LI);
                if (!isStore) {
                  load_map[make_pair(MopPtr, size)] = BI;
                }
                has_alias = true;
                // There cannot be other LOAD operations aliasing the same
                // location.
                break;
              }
            }

            if (!has_alias) {
              if (!isStore) {
                load_map[make_pair(MopPtr, size)] = BI;
              }
              trace.to_instrument.insert(BI);
            }
          }
        }
      }
    }

    bool makeTracePassport(Module &M, Trace &trace) {
      Passport passport;
      bool isStore, isMop;
      int size, src_size, dest_size;
      TraceNumMops = 0;
      FunctionMopCountOnTrace = FunctionMopCount + 1;
      for (BlockSet::iterator TI = trace.blocks.begin(), TE = trace.blocks.end();
           TI != TE; ++TI) {
        for (BasicBlock::iterator BI = (*TI)->begin(), BE = (*TI)->end();
             BI != BE; ++BI) {
          isMop = false;
          if (isa<LoadInst>(BI)) {
            isStore = false;
            isMop = true;
          }
          if (isa<StoreInst>(BI)) {
            isStore = true;
            isMop = true;
          }
          if (isaCallOrInvoke(BI)) {
            // CallInst or maybe other instructions that may cause a jump.
            // TODO(glider): assert that the next instruction is strictly a
            // branch.
            //assert(false);
          }
          if (isMop) {
            if (trace.to_instrument.find(BI) == trace.to_instrument.end())
              continue;
            TraceNumMops++;
            FunctionMopCount++;
            ModuleMopCount++;


            llvm::Instruction &IN = *BI;
            Value *MopPtr;
            if (isStore) {
              MopPtr = (static_cast<StoreInst&>(IN).getPointerOperand());
            } else {
              MopPtr = (static_cast<LoadInst&>(IN).getPointerOperand());
            }
            size = getMopPtrSize(MopPtr, isStore);


            if (MopPtr->getType() != UIntPtr) {
              MopPtr = BitCastInst::CreatePointerCast(MopPtr, UIntPtr, "", BI);
            }

            std::vector<Constant*> mop;
            mop.push_back(getInstructionAddr(FunctionMopCount, BI));
            mop.push_back(ConstantInt::get(Int32, size / 8));
            mop.push_back(ConstantInt::get(Int8, isStore));
            passport.push_back(ConstantStruct::get(MopType, mop));
          }
        }
      }
      if (TraceNumMops) {
        if (InstrumentedTraceCount % kLiteRaceStorageSize == 0) {
          vector <Constant*> counters;
          counters.push_back(ConstantInt::get(Int32, 0));
          counters.push_back(ConstantInt::get(Int32, 0));
          Constant *StorageArray = ConstantArray::get(LiteRaceStorageType,
              vector<Constant*>(kLiteRaceNumTids,
                  ConstantArray::get(LiteRaceStorageLineType,
                      vector<Constant*>(kLiteRaceStorageSize,
                          ConstantStruct::get(LiteRaceCountersType,
                                              counters)))));
          LiteRaceStorageGlob = new GlobalVariable(
            M,
            LiteRaceStorageType,
            /*isConstant*/false,
            GlobalValue::InternalLinkage,
            StorageArray,
            "literace_storage",
            /*ThreadLocal*/false,
            /*AddressSpace*/0
          );
          // TODO(glider): this can be moved to .bss -- need to check.
          LiteRaceStorageGlob->setSection(".data");
        }

        TracePassportType = ArrayType::get(MopType, TraceNumMops);
        LLVMContext &Context = M.getContext();
        BBTraceInfoType = StructType::get(Context,
                                    PlatformInt, PlatformInt,
                                    PlatformInt,
                                    LiteRaceCountersArrayType,
                                    LiteRaceSkipArrayType,
                                    LiteRaceStoragePtrType,
                                    Int32,
                                    TracePassportType,
                                    NULL);

        std::vector<Constant*> trace_info;
        // num_mops_
        trace_info.push_back(ConstantInt::get(PlatformInt, TraceNumMops));
        // pc_
        Instruction *First = trace.entry->begin();
        trace_info.push_back(getInstructionAddr(FunctionMopCountOnTrace,
                                                First));
        // counter_
        trace_info.push_back(ConstantInt::get(PlatformInt, 0));
        // literace_counters[]
        trace_info.push_back(
            ConstantArray::get(LiteRaceCountersArrayType,
                vector<Constant*>(kLiteRaceNumTids,
                                  ConstantInt::get(PlatformInt, 0))));
        // literace_num_to_skip[]
        trace_info.push_back(
            ConstantArray::get(LiteRaceSkipArrayType,
                vector<Constant*>(kLiteRaceNumTids,
                                  ConstantInt::get(PlatformInt, 0))));
        // *literace_storage
        trace_info.push_back(LiteRaceStorageGlob);
        // storage_index
        trace_info.push_back(
            ConstantInt::get(Int32,
                             InstrumentedTraceCount % kLiteRaceStorageSize));
        // mops_
        trace_info.push_back(ConstantArray::get(TracePassportType, passport));

        TracePassportGlob = new GlobalVariable(
            M,
            BBTraceInfoType,
            /*isConstant*/true,
            GlobalValue::InternalLinkage,
            ConstantStruct::get(BBTraceInfoType, trace_info),
            "trace_passport",
            /*ThreadLocal*/false,
            /*AddressSpace*/0
            );
        InstrumentedTraceCount++;
        trace.num_mops = TraceNumMops;
        return true;
      }
      return false;
    }


    void instrumentMop(BasicBlock::iterator &BI, bool isStore,
                       bool check_ident_store, Trace &trace) {
      if (trace.to_instrument.find(BI) == trace.to_instrument.end()) return;
      instrumentation_stats.newMop();
      Value *MopAddr;
      llvm::Instruction &IN = *BI;
      if (isStore) {
        MopAddr = (static_cast<StoreInst&>(IN).getPointerOperand());
      } else {
        MopAddr = (static_cast<LoadInst&>(IN).getPointerOperand());
      }

      if (!check_ident_store || !isStore ||
          !(static_cast<StoreInst&>(IN).getOperand(0)->getType()->isPointerTy())) {
        // Most of the time we don't check the stores for identical values.
        if (MopAddr->getType() == UIntPtr) {
        } else {
          MopAddr = BitCastInst::CreatePointerCast(MopAddr, UIntPtr, "", BI);
        }
      } else {
        // In the first BB of each destructor we check if a store instruction
        // rewrites the memory with the same value. If so, this is a benign
        // race on VPTR and we replace the pointer with NULL.
        // Note that this is done only if rewriting pointers.
        // TODO(glider): in fact this should be done only if accessing _ZVT*

        // How to check that %a is being rewritten with its value?
        // Consider we're instrumenting the following line:
        //             store %new, %a
        // Then the %ptr to be put into TLEB is calculated as follows:
        // %old      = load %a
        // %destcast = ptrtoint %a to i32
        // %neq      = icmp ne %old, %new
        // %neqcast  = zext %neq to i32
        // %intptr   = mul %destcast, %neqcast
        // %ptr      = inttoptr %intptr to i32
        Value *New = static_cast<StoreInst&>(IN).getOperand(0);
        Value *Old = new LoadInst(MopAddr, "", BI);
        Value *DestCast = BitCastInst::CreatePointerCast(MopAddr, ArithmeticPtr, "", BI);
        Value *Neq = new ICmpInst(BI, ICmpInst::ICMP_NE, Old, New, "");
        Value *NeqCast = BitCastInst::CreateZExtOrBitCast(Neq, ArithmeticPtr, "", BI);
        Value *Ptr = BinaryOperator::Create(Instruction::Mul, DestCast, NeqCast, "",
                                            BI);
        MopAddr = new IntToPtrInst(Ptr, UIntPtr, "", BI);
      }

      // Store the pointer into TLEB[TLEBIndex].
      std::vector <Value*> idx;
      idx.push_back(ConstantInt::get(Int32, 0));
      idx.push_back(ConstantInt::get(PlatformInt, TLEBIndex));
      Value *TLEBPtr =
          GetElementPtrInst::Create(TLEB,
                                    idx.begin(),
                                    idx.end(),
                                    "",
                                    BI);
      new StoreInst(MopAddr, TLEBPtr, BI);
      TLEBIndex++;
    }

    // Instrument llvm.memcpy and llvm.memmove.
    void InstrumentMemTransfer(BasicBlock::iterator &BI) {
      MemTransferInst &IN = static_cast<MemTransferInst&>(*BI);
      std::vector <Value*> arg(3);
      arg[0] = IN.getDest();
      if (arg[0]->getType() != UIntPtr) {
        arg[0] = BitCastInst::CreatePointerCast(arg[0], UIntPtr, "", BI);
      }
      arg[1] = IN.getSource();
      if (arg[1]->getType() != UIntPtr) {
        arg[1] = BitCastInst::CreatePointerCast(arg[1], UIntPtr, "", BI);
      }
      arg[2] = IN.getLength();
      if (isa<MemCpyInst>(BI)) {
        ReplaceInstWithInst(BI->getParent()->getInstList(), BI,
                          CallInst::Create(MemCpyFn, arg.begin(), arg.end(),
                                             ""));
                          }
      if (isa<MemMoveInst>(BI)) {
        ReplaceInstWithInst(BI->getParent()->getInstList(), BI,
                          CallInst::Create(MemMoveFn, arg.begin(), arg.end(),
                                             ""));
      }
    }

    // This method is ran only for instrumented basic blocks.
    void runOnBasicBlock(Module &M, BasicBlock *BB,
                         bool first_dtor_bb,
                         Trace &trace) {
      instrumentation_stats.newInstrumentedBasicBlock();
      for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
           BI != BE;
           ++BI) {
        if (isaCallOrInvoke(BI)) {
          llvm::Instruction &IN = *BI;
          if ((isa<CallInst>(BI) &&
               static_cast<CallInst&>(IN).getCalledFunction() == BBFlushFn) ||
              (isa<InvokeInst>(BI) &&
               static_cast<InvokeInst&>(IN).getCalledFunction() == BBFlushFn)) {
            // TODO(glider): we shouldn't encounter BBFlushFn at all.
            // Or not?
///            errs() << "BBFlushFn!\n";
///            assert(false);
            continue;
          }
        }
        if (isa<LoadInst>(BI)) {
          // Instrument LOAD.
          instrumentMop(BI, false, first_dtor_bb, trace);
        }
        if (isa<StoreInst>(BI)) {
          // Instrument STORE.
          instrumentMop(BI, true, first_dtor_bb, trace);
        }
      }
    }

  private:
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
      AU.addRequired<TargetData>();
      AU.addRequired<AliasAnalysis>();
    }
// }}}
  };  // struct TsanOnlineInstrument

  InstrumentationStats::InstrumentationStats() {
    num_functions = 0;
    num_traces = 0;
    num_bbs = 0;
    num_inst_bbs = 0;
    num_inst_bbs_in_trace = -1;
    traces.clear();
    num_mops = 0;
    med_trace_size = -1;
  }

  void InstrumentationStats::newFunction() {
    num_functions++;
  }

  void InstrumentationStats::newTrace() {
    num_traces++;
    if (num_inst_bbs_in_trace > 0) {
      traces.push_back(num_inst_bbs_in_trace);
    }
    num_inst_bbs_in_trace = 0;
  }

  void InstrumentationStats::newBasicBlocks(int num) {
    num_bbs += num;
  }

  void InstrumentationStats::newInstrumentedBasicBlock() {
    num_inst_bbs++;
    num_inst_bbs_in_trace++;
  }

  void InstrumentationStats::newMop() {
    num_mops++;
  }

  void InstrumentationStats::finalize() {
    if (num_inst_bbs_in_trace) {
      traces.push_back(num_inst_bbs_in_trace);
      num_inst_bbs_in_trace = 0;
    }
    sort(traces.begin(), traces.end());
    if (traces.size()) med_trace_size = traces[traces.size() / 2];
  }

  void InstrumentationStats::printStats() {
    finalize();
    errs() << "# of functions in the module: " << num_functions << "\n";
    errs() << "# of traces in the module: " << num_traces << "\n";
    errs() << "median trace size: " << med_trace_size << "\n";
    errs() << "# of basic blocks in the module: " << num_bbs << "\n";
    errs() << "# of instrumented basic blocks in the module: "
           << num_inst_bbs << "\n";
    errs() << "# of memory operations in the module: " << num_mops << "\n";
  }
}

char TsanOnlineInstrument::ID = 0;
RegisterPass<TsanOnlineInstrument> X("online",
    "Compile-time instrumentation for runtime "
    "data race detection with ThreadSanitizer");

// Old-style (pre-2.7) pass initialization.
// TODO(glider): detect the version somehow (LLVM_MINOR_VERSION didn't exist
// before 2.8)
#if 0
INITIALIZE_PASS(TsanOnlineInstrument, "online",
                "Compile-time instrumentation for runtime "
                "data race detection with ThreadSanitizer",
                false, false);
#endif
