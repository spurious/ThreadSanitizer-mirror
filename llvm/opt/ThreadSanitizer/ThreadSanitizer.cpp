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
    WorkaroundVptrRace("workaround-vptr-race",
                       cl::desc("Work around benign races on VPTR in "
                                "destructors"),
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
    InstSet to_instrument;
  };

  typedef vector<Trace> TraceVector;

  struct TsanOnlineInstrument : public ModulePass { // {{{1
    static char ID; // Pass identification, replacement for typeid
    int ModuleFunctionCount, ModuleMopCount, TLEBIndex, OldTLEBIndex;
    Value *TracePassportGlob;
    int TraceCount, TraceNumMops;
    Constant *BBFlushFn;
    // TODO(glider): get rid of rtn_call/rtn_exit at all.
    Constant *RtnCallFn, *RtnExitFn;
    Constant *MemCpyFn, *MemMoveFn;
    const PointerType *UIntPtr, *TraceInfoTypePtr;
    const Type *PlatformInt, *Int8, *ArithmeticPtr, *Int32;
    const Type *Void;
    const StructType *MopType, *TraceInfoType, *BBTraceInfoType;
    const ArrayType *MopArrayType;
    const ArrayType *LiteRaceCountersArrayType, *LiteRaceSkipArrayType;
    const ArrayType *TracePassportType, *TraceExtPassportType;
    const Type *TLEBTy;
    const PointerType *TLEBPtrType;
    Value *ShadowStack;
    const StructType *CallStackType;
    const ArrayType *CallStackArrayType;
    AliasAnalysis *AA;
    static const int kTLEBSize = 100;
    // TODO(glider): hashing constants and BB addresses should be different on
    // x86 and x86-64.
    static const int kBBHiAddr = 5000, kBBLoAddr = 100;
    static const int kFNV1aPrime = 6733, kFNV1aModulo = 2048;
    static const int kMaxAddr = 1 << 30;
    static const int kDebugInfoMagicNumber = 0xdb914f0;
    // TODO(glider): must be in sync with ts_trace_info.h
    static const int kLiteRaceNumTids = 8;
    static const size_t kMaxCallStackSize = 1 << 12;
    int arch_size_;
    int ModuleID;
    set<string> debug_symbol_set;
    set<string> debug_file_set;
    set<string> debug_path_set;
    map<uintptr_t, DebugPcInfo> debug_pc_map;

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

    uintptr_t getAddr(int bb_index, int mop_index,
                      BasicBlock::iterator cur_inst) {
      uintptr_t result = ((ModuleID * kBBHiAddr) + bb_index) * kBBLoAddr + mop_index;
      if (cur_inst) {
        DumpDebugInfo(result, cur_inst);
      }
      if ((result < 0) || (result > kMaxAddr)) {
        errs() << "bb_index: " << bb_index << " mop_index: " << mop_index;
        errs() << " result: " << result << " kMaxAddr: " << kMaxAddr << "\n";
        errs() << "result = ((" << ModuleID << " * " << kBBHiAddr << ") + " <<
          bb_index << ") * " << kBBLoAddr << " + " << mop_index << "\n";
        assert(false);
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
      for (map<uintptr_t, DebugPcInfo>::iterator it = debug_pc_map.begin();
           it != debug_pc_map.end();
           ++it) {
        vector<Constant*> pc;
        pc.push_back(ConstantInt::get(PlatformInt, it->first));
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
          false,
          GlobalValue::InternalLinkage,
          DebugInfo,
          var_id_str,
          false, 0
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
        if (trace.blocks.find(child) == trace.blocks.end()) continue;
        if (used.find(child) != used.end()) return true;
        used.insert(child);
        if (traceHasCycles(trace, child, used)) return true;
        used.erase(child);
      }
      return false;
    }

    // TODO(glider): may want to handle indirectbr.
    // A trace is valid if and only if:
    //  -- it has a single entry point
    //  -- it contains no loops
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

    bool buildClosure(Function::iterator BB,
                      Trace &trace, BlockSet &used) {
      BlockVector to_see;
      to_see.push_back(BB);
      trace.blocks.insert(BB);
      used.insert(BB);
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
          trace.blocks.insert(child);
          if (validTrace(trace)) {
            used.insert(child);
            to_see.push_back(child);
          } else {
            trace.blocks.erase(child);
          }
        }
      }
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
    //   %0 = load i64* getelementptr inbounds (%struct.CallStackPod* @ShadowStack, i64 0, i32 0)
    //   %1 = getelementptr %struct.CallStackPod* @ShadowStack, i64 0, i32 1, i64 %0
    //   store i64 %addr, i64* %1
    //   %2 = load i64* getelementptr inbounds (%struct.CallStackPod* @ShadowStack, i64 0, i32 0)
    //   %3 = add i64 %2, 1
    //   store i64 %3, i64* getelementptr inbounds (%struct.CallStackPod* @ShadowStack, i64 0, i32 0)
    //
    // Or, in C++:
    //
    //   ShadowStack.pcs_[ShadowStack.size_] = (uintptr_t)addr;
    //   ShadowStack.size_++;
    //
    void InsertRtnCall(uintptr_t addr, BasicBlock::iterator &Before) {
#if DEBUG
        std::vector<Value*> inst(1);
        inst[0] = ConstantInt::get(PlatformInt, addr);
        CallInst::Create(RtnCallFn, inst.begin(), inst.end(), "", Before);
#else
      // TODO(glider): each call instruction should update the top of the shadow
      // stack.
      std::vector <Value*> size_idx;
      size_idx.push_back(ConstantInt::get(PlatformInt, 0));
      size_idx.push_back(ConstantInt::get(Int32, 0));
      Value *StackSizePtr =
          GetElementPtrInst::Create(ShadowStack,
                                    size_idx.begin(), size_idx.end(),
                                    "", Before);
      Value *StackSize = new LoadInst(StackSizePtr, "", Before);

      std::vector <Value*> pcs_idx;
      pcs_idx.push_back(ConstantInt::get(PlatformInt, 0));
      pcs_idx.push_back(ConstantInt::get(Int32, 1));
      pcs_idx.push_back(StackSize);
      Value *StackSlotPtr =
          GetElementPtrInst::Create(ShadowStack,
                                    pcs_idx.begin(), pcs_idx.end(),
                                    "", Before);
      Value *Addr = ConstantInt::get(PlatformInt, addr);
      new StoreInst(Addr, StackSlotPtr, Before);

      Value *NewSize = BinaryOperator::Create(Instruction::Add,
                                              StackSize,
                                              ConstantInt::get(PlatformInt, 1),
                                              "", Before);
      new StoreInst(NewSize, StackSizePtr, Before);
#endif
    }

    // Insert the code that pops a stack frame from the shadow stack.
    void InsertRtnExit(BasicBlock::iterator &Before) {
#if DEBUG
      std::vector<Value*> inst(0);
      CallInst::Create(RtnExitFn, inst.begin(), inst.end(), "", Before);
#else

      std::vector <Value*> size_idx;
      size_idx.push_back(ConstantInt::get(PlatformInt, 0));
      size_idx.push_back(ConstantInt::get(Int32, 0));
      Value *StackSizePtr =
          GetElementPtrInst::Create(ShadowStack,
                                    size_idx.begin(), size_idx.end(),
                                    "", Before);
      Value *StackSize = new LoadInst(StackSizePtr, "", Before);
      Value *NewSize = BinaryOperator::Create(Instruction::Sub,
                                              StackSize,
                                              ConstantInt::get(PlatformInt, 1),
                                              "", Before);
      new StoreInst(NewSize, StackSizePtr, Before);
#endif
    }

    virtual bool runOnModule(Module &M) {
      TraceCount = 0;
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
      //   uintptr_t size_;
      //   uintptr_t pcs_[kMaxCallStackSize];
      // };
      CallStackArrayType = ArrayType::get(PlatformInt, kMaxCallStackSize);
      CallStackType = StructType::get(Context,
                                      PlatformInt,
                                      CallStackArrayType,
                                      NULL);
      ShadowStack = new GlobalVariable(M,
                                       CallStackType,
                                       /*isConstant*/true,
                                       GlobalValue::ExternalLinkage,
                                       /*Initializer*/0,
                                       "ShadowStack",
                                       /*InsertBefore*/0,
                                       /*ThreadLocal*/true);
      TLEBTy = ArrayType::get(UIntPtr, kTLEBSize);
      TLEBPtrType = PointerType::get(UIntPtr, 0);

      // void* bb_flush(next_mops)
      BBFlushFn = M.getOrInsertFunction("bb_flush",
                                        TLEBPtrType,
                                        TraceInfoTypePtr, (Type*)0);

      // void rtn_call(void *addr)
      RtnCallFn = M.getOrInsertFunction("rtn_call",
                                        Void,
                                        PlatformInt, (Type*)0);

      // void rtn_exit()
      RtnExitFn = M.getOrInsertFunction("rtn_exit",
                                        Void, (Type*)0);

      MemCpyFn = M.getOrInsertFunction("rtl_memcpy",
                                       UIntPtr,
                                       UIntPtr, UIntPtr, PlatformInt, (Type*)0);
      MemMoveFn = M.getOrInsertFunction("rtl_memmove",
                                       UIntPtr,
                                       UIntPtr, UIntPtr, PlatformInt, (Type*)0);

      // Split each basic block into smaller blocks containing no more than one
      // call instruction at the end.
      for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
        for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
          bool do_split = false;
          for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
               BI != BE;
               ++BI) {
            if (do_split) {
              // No need to split a terminator into a separate block.
              if (!isa<TerminatorInst>(BI)) {
                ///BI->dump();
                SplitBlock(BB, BI, this);
              }
              do_split = false;
              break;
            }
            // A call may not occur inside of a basic block, iff this is not a
            // call to @llvm.dbg.declare
            if (isaCallOrInvoke(BI)) {
              do_split = true;
            }
            if (isa<MemTransferInst>(BI)) {
              InstrumentMemTransfer(BI);
              do_split = true;
            }
          }
        }
      }

      for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
        ModuleFunctionCount++;
        int startTraceCount = TraceCount;
        bool first_dtor_bb = false;

        if (F->isDeclaration()) continue;

        // TODO(glider): document this.
        if ((F->getName()).find("_Znw") != std::string::npos) {
          continue;
        }
        if ((F->getName()).find("_Zdl") != std::string::npos) {
          continue;
        }
        if (isDtor(F->getName())) first_dtor_bb = WorkaroundVptrRace;

        TraceVector traces(buildTraces(*F));
        for (int i = 0; i < traces.size(); ++i) {
          runOnTrace(M, traces[i], first_dtor_bb);
          first_dtor_bb = false;
        }
        BasicBlock::iterator First = F->begin()->begin();
        InsertRtnCall(getAddr(startTraceCount, 0, First), First);
      }
      writeDebugInfo(M);

      return true;
    }

    void runOnTrace(Module &M, Trace &trace, bool first_dtor_bb) {
      Value *TLEB = NULL;
      TLEBIndex = 0;
      TraceCount++;
      markMopsToInstrument(M, trace);
      bool have_passport = makeTracePassport(M, trace);
      bool should_flush = shouldFlushTrace(trace);
      if (shouldFlushTrace(trace)) {
        Instruction *First = trace.entry->begin();
        for (BasicBlock::iterator BI = trace.entry->begin(),
             BE = trace.entry->end();
             BI != BE; ++BI) {
          First = BI;
          if (!isa<PHINode>(First)) break;
        }
        assert(First);
        std::vector <Value*> Args(1);
        std::vector <Value*> idx;
        idx.push_back(ConstantInt::get(PlatformInt, 0));
        if (have_passport) {
          Args[0] =
            GetElementPtrInst::Create(TracePassportGlob,
                                      idx.begin(),
                                      idx.end(),
                                      "",
                                      First);
          Args[0] = BitCastInst::CreatePointerCast(Args[0], TraceInfoTypePtr,
                                                   "", First);
        } else {
          Args[0] = ConstantPointerNull::get(TraceInfoTypePtr);
        }

        TLEB = CallInst::Create(BBFlushFn, Args.begin(), Args.end(), "",
                           First);
      }
      for (BlockSet::iterator TI = trace.blocks.begin(), TE = trace.blocks.end();
           TI != TE; ++TI) {
        runOnBasicBlock(M, *TI, first_dtor_bb, TLEB, trace);
        first_dtor_bb = false;
      }
    }

    void DumpDebugInfo(uintptr_t addr, BasicBlock::iterator BI) {
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

    bool shouldFlushTrace(Trace &trace) {
      for (BlockSet::iterator TI = trace.blocks.begin(), TE = trace.blocks.end();
           TI != TE; ++TI) {
        for (BasicBlock::iterator BI = (*TI)->begin(), BE = (*TI)->end();
             BI != BE; ++BI) {
          if (isa<LoadInst>(BI)) {
            return true;
          }
          if (isa<StoreInst>(BI)) {
            return true;
          }
        }
      }
      return false;
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
            // Falling through.
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
              for (LocMap::iterator LI = store_map.begin(), LE = store_map.end();
                   LI != LE; ++LI) {
                const pair<Value*, int> &location = LI->first;
                AliasAnalysis::AliasResult R = AA->alias(MopPtr, size,
                                                         location.first,
                                                         location.second);
                if ((R == AliasAnalysis::MustAlias) &&
                    (size == location.second)) {
                  ///errs() << "ALIAS\n";
                // TODO(glider): do we need an assertion here?
///                  assert(size == location.second);
                  // We've already seen a STORE of the same size accessing the
                  // same memory location. We're a STORE, too, so drop it.
                      trace.to_instrument.erase(LI->second);
                      trace.to_instrument.insert(BI);
                      store_map.erase(LI);
                      store_map[make_pair(MopPtr, size)] = BI;
                      has_alias = true;
                      ///BI->dump();
                      // There cannot be other STORE operations aliasing the same
                      // location.
                      break;
                } else {
                ///    errs() << "NOALIAS\n";
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
              if ((R == AliasAnalysis::MustAlias) && (size == location.second)) {
 ///               errs() << "ALIAS\n";
                // TODO(glider): do we need an assertion here?
///                assert(size == location.second);
                // Drop the previous access.
                trace.to_instrument.erase(LI->second);
                // It's ok to insert the same BI into to_instrument twice.
                trace.to_instrument.insert(BI);
                load_map.erase(LI);
                if (!isStore) {
                  load_map[make_pair(MopPtr, size)] = BI;
                }
                has_alias = true;
///                BI->dump();
                // There cannot be other STORE operations aliasing the same
                // location.
                break;
              } else {
///                  errs() << "NOALIAS\n";
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
            ModuleMopCount++;


            llvm::Instruction &IN = *BI;
            getAddr(TraceCount, TraceNumMops, BI);
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
            mop.push_back(ConstantInt::get(PlatformInt, getAddr(TraceCount,
                                                                TraceNumMops, BI)));
            mop.push_back(ConstantInt::get(Int32, size / 8));
            mop.push_back(ConstantInt::get(Int8, isStore));
            passport.push_back(ConstantStruct::get(MopType, mop));
          }
        }

      }
      if (TraceNumMops) {
        TracePassportType = ArrayType::get(MopType, TraceNumMops);
        LLVMContext &Context = M.getContext();
        BBTraceInfoType = StructType::get(Context,
                                    PlatformInt, PlatformInt,
                                    PlatformInt,
                                    LiteRaceCountersArrayType,
                                    LiteRaceSkipArrayType,
                                    TracePassportType,
                                    NULL);

        std::vector<Constant*> trace_info;
        // num_mops_
        trace_info.push_back(ConstantInt::get(PlatformInt, TraceNumMops));
        // pc_
        // TODO(glider): the Trace class should provide its entry BB.
        Instruction *First = trace.entry->begin();
        trace_info.push_back(ConstantInt::get(PlatformInt,
                                              getAddr(TraceCount, 0, First)));
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
        // mops_
        trace_info.push_back(ConstantArray::get(TracePassportType, passport));

        TracePassportGlob = new GlobalVariable(
            M,
            BBTraceInfoType,
            false,
            GlobalValue::InternalLinkage,
            ConstantStruct::get(BBTraceInfoType, trace_info),
            "trace_passport",
            false, 0
            );
        return true;
      }
      return false;
    }


    void InstrumentMop(BasicBlock::iterator &BI, bool isStore,
                       Value *TLEB, bool check_ident_store, Trace &trace) {
      if (trace.to_instrument.find(BI) == trace.to_instrument.end()) return;
      Value *MopAddr;
      llvm::Instruction &IN = *BI;
      if (isStore) {
        MopAddr = (static_cast<StoreInst&>(IN).getPointerOperand());
      } else {
        MopAddr = (static_cast<LoadInst&>(IN).getPointerOperand());
      }

      if (!check_ident_store || !isStore) {
        // Most of the time we don't check the stores for identical values.
        if (MopAddr->getType() == UIntPtr) {
        } else {
          MopAddr = BitCastInst::CreatePointerCast(MopAddr, UIntPtr, "", BI);
        }
      } else {
        // In the first BB of each destructor we check if a store instruction
        // rewrites the memory with the same value. If so, this is a benign
        // race on VPTR and we replace the pointer with NULL.

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

      std::vector <Value*> idx;
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

    bool flushBeforeCall(Module &M, BasicBlock::iterator &BI) {
      std::vector <Value*> Args(1);
      Args[0] = ConstantPointerNull::get(TraceInfoTypePtr);
      CallInst::Create(BBFlushFn, Args.begin(), Args.end(), "", BI);
      return true;
    }

    bool runOnBasicBlock(Module &M, BasicBlock *BB,
                         bool first_dtor_bb,
                         Value *TLEB, Trace &trace) {
      bool result = false;
      OldTLEBIndex = 0;
      for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
           BI != BE;
           ++BI) {
        bool unknown = true;  // we just don't want a bunch of nested if()s
        if (isaCallOrInvoke(BI)) {
          //if (isa<MemTransferInst>(BI)) {
          //  InstrumentMemTransfer(BI);
          //}
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
          flushBeforeCall(M, BI);
        }

        if (isa<ReturnInst>(BI)) {
          InsertRtnExit(BI);
          unknown = false;
        }
        if (isa<LoadInst>(BI)) {
          // Instrument LOAD.
          InstrumentMop(BI, false, TLEB, first_dtor_bb, trace);
          unknown = false;
        }
        if (isa<StoreInst>(BI)) {
          // Instrument STORE.
          InstrumentMop(BI, true, TLEB, first_dtor_bb, trace);
          unknown = false;
        }

        if (unknown) {
          // do nothing
        }
      }
    }

  private:
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
      AU.addRequired<TargetData>();
      AU.addRequired<AliasAnalysis>();
    }
  };
  // }}}
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
