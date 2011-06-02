#define DEBUG_TYPE "tsan"

#include "ThreadSanitizer.h"

#include "llvm/Analysis/DebugInfo.h"
#include "llvm/CallingConv.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/InlineAsm.h"
#include "llvm/InstrTypes.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Type.h"

#include "common_util.h"

#include <stdint.h>
#include <stdio.h>

#include <map>
#include <set>
using namespace llvm;
using namespace std;

#if defined(__GNUC__)
# include <cxxabi.h>  // __cxa_demangle
#endif

//#define DEBUG 1
//#define DEBUG_TRACES 1
//#define DEBUG_CYCLES 1
//#define DEBUG_DEBUG_INFO 1
//#define DEBUG_IGNORE_MOPS 1

// Command-line flags. {{{1
static cl::opt<string>
    TargetArch("arch",
               cl::desc("Target arch: x86 or x64"));
static cl::opt<string>
    IgnoreFile("ignore",
               cl::desc("File containing the list of functions to ignore "
                        "during instrumentation"));
static cl::opt<bool>
    InstrumentAll("instrument-all",
                  cl::desc("Do not optimize the instrumentation "
                           "of memory operations"),
                  cl::init(false));
static cl::opt<bool>
    PrintStats("print-stats",
                  cl::desc("Print the instrumentation stats"),
                  cl::init(true));

static cl::opt<bool>
    WorkaroundVptrRace("workaround-vptr-race",
                       cl::desc("Work around benign races on VPTR in "
                                "destructors"),
                       cl::init(true));

static cl::opt<bool>
    EnableLiteRaceSampling("enable-literace-sampling",
                           cl::desc("Flush hot traces less frequently"),
                           cl::init(true));

static cl::opt<bool>
    EnableFunctionInstrumentation("enable-function-instrumentation",
                      cl::desc("Update the shadow stack upon function "
                               "entries/exits"),
                      cl::init(true));
static cl::opt<bool>
    InlineShadowStackUpdates("inline-shadow-stack-updates",
                             cl::desc("Write to shadow stack directly instead "
                                      "of calling rtn_call() and rtn_exit()"),
                             cl::init(true));

static cl::opt<bool>
    CheckShadowStackConsistency("check-shadow-stack-consistency",
        cl::desc("At the end of each function pass the initial and final values "
                 "of ShadowStack.end_ to the runtime to compare them."),
        cl::init(false));

static cl::opt<bool>
    EnableMemoryInstrumentation("enable-memory-instrumentation",
                                cl::desc("Instrument memory operations"),
                                cl::init(true));

static cl::opt<bool>
    EnableTraceFlushing("enable-trace-flushing",
        cl::desc("Insert a flush after each trace "
                 "(otherwise the race detector just won't notice it)"),
        cl::init(true));

static cl::opt<bool>
    DoNothing("do-nothing",
              cl::desc("Do not modify the code, exit immediately"),
              cl::init(false));

static cl::opt<bool>
    IgnoreMopsByOrigin("ignore-mops-by-origin",
                       cl::desc("Traverse the stack of inlined functions to "
                                "decide whether each memory operation should "
                                "be ignored. Experimental feature."),
                      cl::init(true));

static cl::opt<bool>
    SkipFunctionsWithoutMops("skip-functions-without-mops",
                             cl::desc("Do not instrument functions "
                                      "containing no memory operations"),
                             cl::init(false));

static cl::opt<bool>
    BasicBlocksAreTraces("basic-blocks-are-traces",
        cl::desc("Each basic block is treated as a single trace"),
        cl::init(false));

static cl::opt<bool>
    UseTlebForMinimalBlocks("use-tleb-for-minimal-blocks",
        cl::desc("Pass blocks containing a single mop via TLEB"),
        cl::init(false));

// }}}

// Required by OpenFileReadOnly in common_util.h
void Report(const char *format, ...) {
  int buff_size = 1024*16;
  char *buff = new char[buff_size];
  CHECK(buff);

  va_list args;

  while (1) {
    va_start(args, format);
    int ret = vsnprintf(buff, buff_size, format, args);
    va_end(args);
    if (ret < buff_size) break;
    delete [] buff;
    buff_size *= 2;
    buff = new char[buff_size];
    CHECK(buff);
    // Printf("Resized buff: %d\n", buff_size);
  }
  errs() << buff;
}

namespace {

// TsanOnlineInstrument implementation {{{1
TsanOnlineInstrument::TsanOnlineInstrument() : ModulePass(ID) {
  if (TargetArch == "x86-64") {
    ArchSize = 64;
  } else {
    ArchSize = 32;
  }
  if (IgnoreFile.size()) {
    parseIgnoreFile(IgnoreFile);
  }
}

// instruction_address = function_address + c_offset
// (number of mops is always less or equal to the function size)
Constant *TsanOnlineInstrument::getInstructionAddr(
    int mop_index, BasicBlock::iterator &cur_inst,
    const IntegerType *ResultType) {
  Value *cur_fun = cur_inst->getParent()->getParent();
  Constant *c_offset = ConstantInt::get(ResultType, mop_index);
  Constant *result =
      ConstantExpr::getAdd(
          ConstantExpr::getPtrToInt(cast<Constant>(cur_fun), ResultType),
          c_offset);
  dumpInstructionDebugInfo(result, cur_inst);
  return result;
}

uintptr_t TsanOnlineInstrument::getModuleID(Module &M) {
  uintptr_t result = 0;
  char tmp;
  string name = M.getModuleIdentifier();
  for (size_t i = 0; i < name.size(); i++) {
    tmp = name[i];
    result = (result ^ tmp) % kFNV1aModulo;
    result = (result * kFNV1aPrime) % kFNV1aModulo;
  }
  return result;
}

bool TsanOnlineInstrument::isDtor(const string &mangled_name) {
  int status;
  char *demangled = NULL;
#if defined(__GNUC__)
  demangled = __cxxabiv1::__cxa_demangle(mangled_name.c_str(),
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
    if (mangled_name.find("D0") != string::npos) return true;
    if (mangled_name.find("D1") != string::npos) return true;
  }
  return false;
}

void TsanOnlineInstrument::writeModuleDebugInfo(Module &M) {
  // The debug info is stored in a per-module global structure named
  // "rtl_debug_info${ModuleID}".
  // TODO(glider): this may lead to name collisions.
  LLVMContext &Context = M.getContext();
  vector<Constant*> dummy;
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
    for (size_t i = 0; i < it->size(); i++) {
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
    for (size_t i = 0; i < it->size(); i++) {
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
    for (size_t i = 0; i < it->size(); i++) {
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

BlockSet &TsanOnlineInstrument::getPredecessors(BasicBlock *bb) {
  return predecessors[bb];
}

bool TsanOnlineInstrument::visit(BasicBlock *node,
                                 Trace &trace,
                                 BlockSet &visited) {
  if (!visited.count(node)) {
    visited.insert(node);
    TerminatorInst *BBTerm = node->getTerminator();
    for (int i = 0, e = BBTerm->getNumSuccessors(); i != e; ++i) {
      BasicBlock *child = BBTerm->getSuccessor(i);
      if (!trace.blocks.count(child)) continue;
      if (!visit(child, trace, visited)) return false;
    }
    return false;  // we haven't visited |node| before
  } else {
    return true;  // we're seeing |node| for the second time
  }
}

// Sort the trace topologically to check whether there are cycles.
bool TsanOnlineInstrument::traceHasCycles(Trace &trace) {
#ifdef DEBUG_CYCLES
    errs() << "LOOKING FOR CYCLES: [ ";
    for (BlockSet::iterator I = trace.blocks.begin(),
                            E = trace.blocks.end();
         I != E; ++I) {
      errs() << (*I)->getName() << " ";
    }
    errs() << "]\n";
#endif
  BlockSet visited;
  return visit(trace.entry, trace, visited);
}

// Validate the trace and populate the list of its exits.
// A trace is valid if and only if:
//  -- it has a single entry point
//  -- it contains no loops
// TODO(glider): may want to handle indirectbr.
bool TsanOnlineInstrument::validateTrace(Trace &trace) {
  BlockSet entries;
  BlockSet accessible;
  int num_edges = 0;
#ifdef DEBUG_TRACES
    errs() << "VALIDATING: [ ";
    for (BlockSet::iterator I = trace.blocks.begin(), E = trace.blocks.end();
         I != E; ++I) {
      errs() << (*I)->getName() << " ";
    }
    errs() << "]\n";
#endif

  // Find all the entry points.
  for (BlockSet::iterator BB = trace.blocks.begin(), E = trace.blocks.end();
       BB != E; ++BB) {
    BlockSet &pred = getPredecessors(*BB);
    Function *F = (*BB)->getParent();
    BasicBlock *entry = F->begin();
#ifdef DEBUG_TRACES
    errs() << "  next block: " << (*BB)->getName() << "\n";
#endif
    if (*BB == entry) {
      entries.insert(*BB);
#ifdef DEBUG_TRACES
      errs() << "  main entry block: " << (*BB)->getName() << "\n";
#endif
    }
    for (BlockSet::iterator I = pred.begin(), E = pred.end();
         I != E; ++I) {
#ifdef DEBUG_TRACES
      errs() << "  " << (*I)->getName() << "->" << (*BB)->getName() << "\n";
#endif
      if (trace.blocks.count(*I)) {
        num_edges++;
      } else {
        entries.insert(*BB);
#ifdef DEBUG_TRACES
        errs() << "  entry block: " << (*BB)->getName() << "\n";
#endif
      }
    }
  }
  if (entries.size() > 1) {
#ifdef DEBUG_TRACES
    errs() << "  ERROR: MULTIPLE ENTRY DETECTED\n\n";
#endif
    return false;
  }
  // TODO(glider): looks like the greedy algorithm allows us not to check
  // for cycles inside trace. But we may want to assert that the only
  // possible cycle can lead to the entry block from within the trace.
  // TODO(glider): no need for redundant cycle checks
  if (traceHasCycles(trace)) {
#ifdef DEBUG_TRACES
    errs() << "LOOP DETECTED\n";
#endif
    return false;
  }

  // Fill the list of trace exits.
  trace.exits.clear();
  for (BlockSet::iterator BB = trace.blocks.begin(), E = trace.blocks.end();
       BB != E; ++BB) {
    TerminatorInst *BBTerm = (*BB)->getTerminator();
    // Iff any successor of a basic block doesn't belong to a trace,
    // then no successors of that basic block should belong to the trace.
    if (BBTerm->getNumSuccessors() > 0) {
      BasicBlock *child = BBTerm->getSuccessor(0);
      bool has_ext_successors = !trace.blocks.count(child);
      if (has_ext_successors) {
        trace.exits.insert(*BB);
      }
      for (int i = 0, e = BBTerm->getNumSuccessors(); i != e; ++i) {
        BasicBlock *child = BBTerm->getSuccessor(i);
        // If any basic block loops to the trace entry, the trace is
        // invalid. It's guaranteed that no basic block loops to itself.
        if (child == trace.entry) {
#ifdef DEBUG_TRACES
  errs() << "  ERROR: CHILD LOOPING TO THE TRACE ENTRY\n";
#endif
          return false;
        }
        bool is_external = !trace.blocks.count(child);
        if (has_ext_successors != is_external) {
#ifdef DEBUG_TRACES
  errs() << "  ERROR: CHILD HAVING BOTH EXT AND NON-EXT SUCCESSORS\n";
#endif
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
#ifdef DEBUG_TRACES
  errs() << "  VALIDATION OK\n";
#endif
  return true;
}

// Build a valid trace starting at |Entry| and containing no basic blocks
// from |used|. Write that trace into |trace|.
//
// Starting with a single basic block, create the trace T2 from T1
// according to the following rule:
//  -- for each basic blocks P, C1, C2 such as
//          (P in T1) and (P->C1) and (P->C2):
//     (C1 in T2) if and only if (C2 in T2)
void TsanOnlineInstrument::buildClosureInner(Trace &trace, BlockSet &used) {
  BlockSet children;
  for (BlockSet::iterator SI = trace.blocks.begin(),
                          SE = trace.blocks.end();
               SI != SE; ++SI) {
    TerminatorInst *BBTerm = (*SI)->getTerminator();
    for (int i = 0, e = BBTerm->getNumSuccessors(); i != e; ++i) {
      BasicBlock *child = BBTerm->getSuccessor(i);
      if (trace.blocks.count(child)) continue;
      // If a child doesn't belong to this trace, we can't use it and thus
      // any other child of this node.
      if (used.count(child)) {
        children.clear();
        break;
      }
      Instruction *First = child->begin();
      if (isa<CallInst>(First) || isa<InvokeInst>(First)) {
        /// Child basic block starts with a call -- it can't be added.
        children.clear();
        break;
      }
      children.insert(child);
    }
    if (children.empty()) break;
  }
  if (children.empty()) {
    return;
  } else {
    for (BlockSet::iterator CI = children.begin(),
                            CE = children.end();
             CI != CE; ++CI) {
        trace.blocks.insert(*CI);
    }
    if (!traceHasCycles(trace)) {
      buildClosureInner(trace, used);
      if (validateTrace(trace)) {
        return;
      } else {
        for (BlockSet::iterator CI = children.begin(),
                                CE = children.end();
                 CI != CE; ++CI) {
          trace.blocks.erase(*CI);
        }
      }
    } else {
      errs() << "Has cycles\n";
      for (BlockSet::iterator CI = children.begin(),
                              CE = children.end();
               CI != CE; ++CI) {
        trace.blocks.erase(*CI);
      }
    }
  }
}

void TsanOnlineInstrument::buildClosure(Trace &trace, BlockSet &used) {
#ifdef DEBUG_TRACES
  errs() << "buildClosure(" << (*trace.blocks.begin())->getName() << ")\n";
#endif
  if (!BasicBlocksAreTraces) buildClosureInner(trace, used);
  // Otherwise proceed with a trace consisting of a single basic block.
  assert(validateTrace(trace));
#if DEBUG_TRACES
  errs() << "TRACE: [ ";
  for (BlockSet::iterator I = trace.blocks.begin(), E = trace.blocks.end();
       I != E; ++I) {
    errs() << (*I)->getName() << " ";
  }
  errs() << "]\n";
#endif
  // A valid trace should have at least one exit!
  assert(trace.exits.size());
}

// Cache the predecessors for each basic block within a function.
void TsanOnlineInstrument::cachePredecessors(Function &F) {
  for (Function::iterator BB = F.begin(), E = F.end(); BB != E; ++BB) {
    TerminatorInst *BBTerm = BB->getTerminator();
    for (int i = 0, e = BBTerm->getNumSuccessors(); i != e; ++i) {
      BasicBlock *child = BBTerm->getSuccessor(i);
      predecessors[child].insert(BB);
    }
  }
}

TraceVector TsanOnlineInstrument::buildTraces(Function &F) {
  TraceVector traces;
  BlockSet used_bbs;
  BlockVector to_see;
  BlockSet visited;
  cachePredecessors(F);
  Trace *current_trace = new Trace;
  to_see.push_back(F.begin());
  for (size_t i = 0; i < to_see.size(); ++i) {
    BasicBlock *current_bb = to_see[i];
    if (!used_bbs.count(current_bb)) {
      assert(current_trace->blocks.size() == 0);
      current_trace->entry = current_bb;
      current_trace->blocks.insert(current_bb);
      buildClosure(*current_trace, used_bbs);
      for (BlockSet::iterator SI = current_trace->blocks.begin(),
                              SE = current_trace->blocks.end();
               SI != SE; ++SI) {
        used_bbs.insert(*SI);
      }
      assert(current_trace->exits.size());
      traces.push_back(current_trace);
      current_trace = new Trace;
    }
    TerminatorInst *BBTerm = current_bb->getTerminator();
    for (int ch = 0, e = BBTerm->getNumSuccessors(); ch != e; ++ch) {
      BasicBlock *child = BBTerm->getSuccessor(ch);
      if (!visited.count(child)) {
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
// TODO(glider): the comment above is totally obsolete and should be
// rewritten. We insert the code that increments the stack top pointer,
// so that there's an uninitialized value on the top.
// This saves us an instruction and is acceptable, because further calls and/or
// basic block flushes should update the stack top.
//
// The effective C++ code for this is:
//   ShadowStack.end_++;
//
void TsanOnlineInstrument::insertRtnCall(Constant *addr,
                                         BasicBlock::iterator &Before) {
  if (!EnableFunctionInstrumentation) return;
  if (!InlineShadowStackUpdates) {
    vector<Value*> inst(1);
    inst[0] = addr;
    CallInst::Create(RtnCallFn, inst.begin(), inst.end(), "", Before);
  } else {
    vector <Value*> end_idx;
    end_idx.push_back(ConstantInt::get(PlatformInt, 0));
    end_idx.push_back(ConstantInt::get(Int32, 0));
    Value *StackEndPtr =
        GetElementPtrInst::Create(ShadowStack,
                                  end_idx.begin(), end_idx.end(),
                                  "", Before);
    CurrentStackEnd = new LoadInst(StackEndPtr, "", Before);

    vector <Value*> new_idx;
    new_idx.push_back(ConstantInt::get(Int32, 1));
    Value *NewStackEnd =
        GetElementPtrInst::Create(CurrentStackEnd,
                                  new_idx.begin(), new_idx.end(),
                                  "", Before);
    new StoreInst(NewStackEnd, StackEndPtr, Before);
  }
}

// Insert the code that pops a stack frame from the shadow stack.
void TsanOnlineInstrument::insertRtnExit(BasicBlock::iterator &Before) {
  if (!EnableFunctionInstrumentation) return;
  if (!InlineShadowStackUpdates) {
    vector<Value*> inst(0);
    CallInst::Create(RtnExitFn, inst.begin(), inst.end(), "", Before);
  } else {
    vector <Value*> end_idx;
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
    assert(CurrentStackEnd);  // this is the original value
    if (CheckShadowStackConsistency) {
      vector <Value*> end_idx;
      end_idx.push_back(ConstantInt::get(PlatformInt, 0));
      end_idx.push_back(ConstantInt::get(Int32, 0));
      Value *StackEndPtr =
          GetElementPtrInst::Create(ShadowStack,
                                    end_idx.begin(), end_idx.end(),
                                    "", Before);
      Value *StackEnd = new LoadInst(StackEndPtr, "", Before);

      vector <Value*> new_idx;
      new_idx.push_back(ConstantInt::get(Int32, -1));
      Value *NewStackEnd =
          GetElementPtrInst::Create(StackEnd,
                                    new_idx.begin(), new_idx.end(),
                                    "", Before);
      new StoreInst(NewStackEnd, StackEndPtr, Before);
      vector <Value*> check;
      check.push_back(CurrentStackEnd);
      check.push_back(NewStackEnd);
      CallInst::Create(ShadowStackCheckFn, check.begin(), check.end(),
                       "", Before);
    } else {
      new StoreInst(CurrentStackEnd, StackEndPtr, Before);
    }
  }
}

// TODO(glider): do we need this function?
int TsanOnlineInstrument::numMopsInFunction(Module::iterator &F) {
  int result = 0;
  for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
    for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
         BI != BE;
         ++BI) {
      if (isa<LoadInst>(BI) || isa<StoreInst>(BI)) {
        result++;
      }
    }
  }
  return result;
}

// TsanOnlineInstrument is a module pass, so this is just a helper function, not
// an interface implementation.
void TsanOnlineInstrument::runOnFunction(Module::iterator &F) {
  ModuleFunctionCount++;
  FunctionMopCount = 0;
  int num_mops_in_traces = 0;
  TracePassportGlob = NULL;
  bool first_dtor_bb = false;
  bool ignore_recursively = false;

  if (F->isDeclaration()) return;
  if (shouldIgnoreFunction(*F)) return;

  if (!shouldIgnoreFunctionRecursively(*F)) {
    // We shouldn't ignore the function -- instrument it.

    // TODO(glider): document this.
    // I even can't remember why in the world we do skip new/delete.
    // Probably should be removed: if someone has implemented his own
    // new/delete operators, we definitely do not want to compile them.
    // Upd: looks like these are placement new/delete operators. Still can't
    // understand why not instrument them.
    if ((F->getName()).find("_Znw") != string::npos) {
      //F->dump();
      //assert(false)
      return;
    }
    if ((F->getName()).find("_Zdl") != string::npos) {
      //F->dump();
      //assert(false)
      return;
    }
    // TODO(glider): rely on the vtable mangled name instead of first_dtor_bb.
    if (isDtor(F->getNameStr())) first_dtor_bb = WorkaroundVptrRace;

    instrumentation_stats.newFunction();
    instrumentation_stats.newBasicBlocks(F->size());
#if (defined(DEBUG_TRACES) || defined(DEBUG_IGNORE_MOPS))
    errs() << "\n\nFUNCTION: " << F->getName() << "\n";
    F->dump();
#endif

    // Build the traces. Note that every basic block should belong to some
    // trace, even if it doesn't contain any memory operations.
    TraceVector traces(buildTraces(*F));
    for (size_t i = 0; i < traces.size(); ++i) {
      assert(traces[i]->exits.size());
      markMopsToInstrument(*traces[i]);
      num_mops_in_traces += traces[i]->num_mops;
    }

    // TODO(glider): if we skip the function instrumentation, we also do not
    // update the shadow stack frame. This may speed up small functions (there
    // are lots of them if we compile with -fno-inline-functions), but we may
    // also lose stack pecision if the skipped function calls instrumented
    // functions. Controversial, need to evaluate.
    if (SkipFunctionsWithoutMops && !num_mops_in_traces) return;
    // runOnTrace() instruments function calls, so we need to check
    // SkipFunctionsWithoutMops first.

    // Instrument the traces.
    for (size_t i = 0; i < traces.size(); ++i) {
      assert(traces[i]->exits.size());
      runOnTrace(*(traces[i]), first_dtor_bb);
      first_dtor_bb = false;
    }
  } else {
    // Ignore the memory operations in the function.
    ignore_recursively = true;
  }


  // Instrument routine calls and exits. Also update the shadow stack every time
  // we meet a call/invoke instruction, otherwise we'll get uninitialized frames
  // in the middle of the stack.
  //
  // insertRtnExit() uses the shadow stack size obtained by
  // insertRtnCall() and should be always executed after it.
  // TODO(glider): most likely we don't need to reuse an expression,
  // let the optimizer do its job.
  BasicBlock::iterator First = F->begin()->begin();
  insertRtnCall(getInstructionAddr(0, First, PlatformInt), First);
  if (ignore_recursively) insertIgnoreInc(First);
  for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
    for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
         BI != BE;
         ++BI) {
      if (isa<ReturnInst>(BI)) {
        if (ignore_recursively) insertIgnoreDec(BI);
        insertRtnExit(BI);
      }
      if (ignore_recursively && isaCallOrInvoke(BI) &&
          calls_to_instrument.count(BI)) {
        instrumentCall(BI);
      }
    }
  }
}

void TsanOnlineInstrument::setupDataTypes() {
  // Arch size dependent types.
  if (ArchSize == 64) {
    UIntPtr = Type::getInt64PtrTy(*ThisModuleContext);
    PlatformInt = Type::getInt64Ty(*ThisModuleContext);
    ArithmeticPtr = Type::getInt64Ty(*ThisModuleContext);
    PlatformPc = IntegerType::get(*ThisModuleContext, 48);
  } else {
    UIntPtr = Type::getInt32PtrTy(*ThisModuleContext);
    PlatformInt = Type::getInt32Ty(*ThisModuleContext);
    ArithmeticPtr = Type::getInt32Ty(*ThisModuleContext);
    PlatformPc = IntegerType::get(*ThisModuleContext, 32);
  }

  Int1 = Type::getInt1Ty(*ThisModuleContext);
  Int4 = IntegerType::get(*ThisModuleContext, 4);
  Int8 = Type::getInt8Ty(*ThisModuleContext);
  Int8Ptr = Type::getInt8PtrTy(*ThisModuleContext);
  Int32 = Type::getInt32Ty(*ThisModuleContext);
  Int64 = Type::getInt64Ty(*ThisModuleContext);
  Void = Type::getVoidTy(*ThisModuleContext);

  // MopType represents the following class declared in ts_trace_info.h:
  // struct MopInfo {
  //   uintptr_t pc;
  //   uint32_t  size;
  //   bool      is_write;
  // };
  // TODO(glider): the old MopInfo layout is deprecated and should be removed.
  MopType = StructType::get(*ThisModuleContext, PlatformInt, Int32, Int8, NULL);

  // MopType64 represents a 64-bit union of the following layout:
  // struct MopInfo {
  //   ...
  //  private:
  //   uintptr_t  size_minus1_   :4;  // 0..15
  //   uintptr_t  is_write_      :1;
  //   uintptr_t  create_sblock_ :1;
  // #if __WORDSIZE == 64
  //   uintptr_t  pc_            :48;  // 48 bits is enough for pc.
  // #else
  //   uintptr_t  pc_;
  // #endif
  // };
  //
  // BUT we use a single 64-bit number instead.
  MopType64 = Type::getInt64PtrTy(*ThisModuleContext);

  MopArrayType = ArrayType::get(MopType64, kTLEBSize);
  LiteRaceCountersArrayType = ArrayType::get(Int32, kLiteRaceNumTids);
  LiteRaceSkipArrayType = ArrayType::get(Int32, kLiteRaceNumTids);

  // struct LiteRaceCounters {
  //   uint32_t counter;
  //   int32_t num_to_skip;
  // };
  LiteRaceCountersType = StructType::get(*ThisModuleContext,
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
  //  enum { kLiteRaceNumTids = 8 };
  //  enum { kLiteRaceStorageSize = 8 };
  //  typedef LiteRaceCounters LiteRaceStorage[kLiteRaceNumTids][kLiteRaceStorageSize];
  //
  //  size_t n_mops_;
  //  size_t pc_;
  //  size_t counter_;
  //  LiteRaceStorage *literace_storage;
  //  int32_t storage_index;
  //  MopInfo mops_[1];
  // };
  TraceInfoType = StructType::get(*ThisModuleContext,
                                PlatformInt, PlatformInt, PlatformInt,
                                LiteRaceStoragePtrType,
                                Int32,
                                MopArrayType,
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
  //
  // Note that |end_| points to the first invalid stack frame, i.e. the current
  // stack frame is at *(end_ - 1).
  CallStackArrayType = ArrayType::get(PlatformInt, kMaxCallStackSize);
  CallStackType = StructType::get(*ThisModuleContext,
                                  UIntPtr,
                                  CallStackArrayType,
                                  NULL);

}

void TsanOnlineInstrument::setupRuntimeGlobals() {
  ShadowStack = new GlobalVariable(*ThisModule,
                                   CallStackType,
                                   /*isConstant*/false,
                                   GlobalValue::ExternalWeakLinkage,
                                   /*Initializer*/0,
                                   "ShadowStack",
                                   /*InsertBefore*/0,
                                   /*ThreadLocal*/true);
  ThreadLocalIgnore = new GlobalVariable(*ThisModule,
                                         PlatformInt,
                                         /*isConstant*/false,
                                         GlobalValue::ExternalWeakLinkage,
                                         /*Initializer*/0,
                                         "thread_local_ignore",
                                         /*InsertBefore*/0,
                                         /*ThreadLocal*/true);
  LiteraceTid = new GlobalVariable(*ThisModule,
                                   PlatformInt,
                                   /*isConstant*/false,
                                   GlobalValue::ExternalWeakLinkage,
                                   /*Initializer*/0,
                                   "LTID",
                                   /*InsertBefore*/0,
                                   /*ThreadLocal*/true);
  TLEBTy = ArrayType::get(UIntPtr, kTLEBSize);
  TLEBPtrType = PointerType::get(UIntPtr, 0);
  TLEB = new GlobalVariable(*ThisModule,
                            TLEBTy,
                            /*isConstant*/true,
                            GlobalValue::ExternalWeakLinkage,
                            /*Initializer*/0,
                            "TLEB",
                            /*InsertBefore*/0,
                            /*ThreadLocal*/true);

  // void* bb_flush(next_mops)
  // TODO(glider): need to stop supporting it. This has been broken for months.
  BBFlushFn = ThisModule->getOrInsertFunction("bb_flush",
                                              Void,
                                              TraceInfoTypePtr, (Type*)0);
  cast<Function>(BBFlushFn)->setLinkage(Function::ExternalWeakLinkage);

  // void* bb_flush_current(cur_mops)
  // TODO(glider): need another name, because we now flush superblocks, not
  // basic blocks.
  BBFlushCurrentFn = ThisModule->getOrInsertFunction("bb_flush_current",
                                                     Void,
                                                     TraceInfoTypePtr, (Type*)0);
  cast<Function>(BBFlushCurrentFn)->
      setLinkage(Function::ExternalWeakLinkage);

  // void* bb_flush_mop(cur_mop, addr)
  BBFlushMop =
      ThisModule->getOrInsertFunction("bb_flush_mop",
                                      Void,
                                      TraceInfoTypePtr, UIntPtr, (Type*)0);
  cast<Function>(BBFlushCurrentFn)->
      setLinkage(Function::ExternalWeakLinkage);



  // void rtn_call(void *addr)
  // We should keep this for debugging purpose.
  RtnCallFn = ThisModule->getOrInsertFunction("rtn_call",
                                              Void,
                                              PlatformInt, (Type*)0);

  // void rtn_exit()
  // We should keep this for debugging purpose.
  RtnExitFn = ThisModule->getOrInsertFunction("rtn_exit",
                                              Void, (Type*)0);

  ShadowStackCheckFn = ThisModule->getOrInsertFunction("shadow_stack_check",
                                                       Void,
                                                       UIntPtr, UIntPtr,
                                                       (Type*)0);

  MemCpyFn =
      ThisModule->getOrInsertFunction("rtl_memcpy",
                                      UIntPtr,
                                      UIntPtr, UIntPtr, PlatformInt, (Type*)0);
  cast<Function>(MemCpyFn)->setLinkage(Function::ExternalWeakLinkage);
  MemMoveFn =
      ThisModule->getOrInsertFunction("rtl_memmove",
                                      UIntPtr,
                                      UIntPtr, UIntPtr, PlatformInt, (Type*)0);
  cast<Function>(MemMoveFn)->setLinkage(Function::ExternalWeakLinkage);
  const Type *Tys[] = { PlatformInt };
  MemSetIntrinsicFn = Intrinsic::getDeclaration(ThisModule,
                                                Intrinsic::memset,
                                                Tys, /*numTys*/1);

}

// virtual
bool TsanOnlineInstrument::runOnModule(Module &M) {
  if (DoNothing) return false;
  InstrumentedTraceCount = 0;
  ModuleFunctionCount = 0;
  ModuleMopCount = 0;
  ModuleID = getModuleID(M);
  ThisModule = &M;
  ThisModuleContext = &(M.getContext());
  AA = &getAnalysis<AliasAnalysis>();
  TD = getAnalysisIfAvailable<TargetData>();
  // If TargetData is unavailable, do nothing.
  if (!TD) return false;
  setupDataTypes();
  setupRuntimeGlobals();

  // Split each basic block into smaller blocks containing no more than one
  // call instruction at the end.
  // TODO(glider): rewrite this comment.

  // Split each basic block so that:
  //  -- it doesn't contain calls after memory operations (we should be
  //     able to insert a flush before such a call)
  //  -- it doesn't branch to itself
  //  -- if it starts with a call, there is an empty basic block before it
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    // We do not want to split on a call instruction more than once.
    SmallSet<Instruction*, 32> already_splitted;
    for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
      bool need_split = false;
      BasicBlock::iterator OldBI = BB->end();
      for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
           BI != BE;
           ++BI) {
        if (isa<LoadInst>(BI) || isa<StoreInst>(BI)) {
          need_split = true;
        }
        // An INVOKE is treated as CALL, not as a regular terminator.
        // TODO(glider): this should be fixed once we support exceptions.
        if (isa<TerminatorInst>(BI) && !(isa<InvokeInst>(BI))) {
          // If a basic block branches to itself, split it into two basic
          // blocks.
          for (int i = 0,
                   e = cast<TerminatorInst>(BI)->getNumSuccessors();
               i != e; ++i) {
            BasicBlock *child = cast<TerminatorInst>(BI)->getSuccessor(i);
            if (child == BB) {
              SplitBlock(BB, BI, this);
              break;
            }
          }
          if (isa<ReturnInst>(BI) && need_split) {
            SplitBlock(BB, BI, this);
          }
          break;
        }
        // A call may not occur inside of a basic block, iff this is not a
        // call to @llvm.dbg.declare
        if (isaCallOrInvoke(BI)) {
          // Mark the CALL/INVOKE instruction as to be instrumented.
          calls_to_instrument.insert(BI);
          if (!already_splitted.count(BI) && (need_split || BB!=F->begin())) {
            already_splitted.insert(BI);
            SplitBlock(BB, BI, this);
            need_split = false;
            break;
          }
        }
        if (isa<MemTransferInst>(BI)) {
          instrumentMemTransfer(BI);
          if ((need_split || BI == BB->begin()) &&
              !already_splitted.count(BI)) {
            already_splitted.insert(BI);
            SplitBlock(BB, BI, this);
            need_split = false;
            break;
          }
        }
      }
    }
  }

  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    runOnFunction(F);
  }
  writeModuleDebugInfo(M);
  if (PrintStats) instrumentation_stats.printStats();
  // We do insert some external declarations, so return true to determine
  // that the pass has modified the IR.
  return true;
}

void TsanOnlineInstrument::insertIgnoreInc(
    llvm::BasicBlock::iterator &Before) {
  Value *Old = new LoadInst(ThreadLocalIgnore, "", Before);
  Value *New =
      BinaryOperator::CreateAdd(Old,
                                ConstantInt::get(PlatformInt, 1),
                                "",
                                Before);
  new StoreInst(New, ThreadLocalIgnore, Before);
}

void TsanOnlineInstrument::insertIgnoreDec(
    llvm::BasicBlock::iterator &Before) {
  Value *Old = new LoadInst(ThreadLocalIgnore, "", Before);
  Value *New =
      BinaryOperator::CreateSub(Old,
                                ConstantInt::get(PlatformInt, 1),
                                "",
                                Before);
  new StoreInst(New, ThreadLocalIgnore, Before);

}

// TODO(glider): either allow the user to use bb_flush() and implement the
// sampling instrumentation, or delete this.
void TsanOnlineInstrument::insertFlushCall(Trace &trace, Instruction *Before) {
  assert(Before);
  if (!EnableLiteRaceSampling) {
    // Sampling is off -- just insert an unconditional call to bb_flush().
    vector <Value*> Args(1);
    vector <Value*> idx;
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
    // TODO(glider): unimplemented
  }
}

// |MopAddr| is ignored iff |useTLEB| == true.
void TsanOnlineInstrument::insertFlushCurrentCall(Trace &trace,
                                                  Instruction *Before,
                                                  bool useTLEB,
                                                  Value *MopAddr) {
  assert(Before);
  if (!EnableLiteRaceSampling) {
    // Sampling is off -- just insert an unconditional call to bb_flush().
    vector <Value*> Args(1);
    vector <Value*> idx;
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
    if (EnableTraceFlushing) {
      CallInst::Create(BBFlushCurrentFn,
                       Args.begin(), Args.end(), "", Before);
    }
  } else {
    // Sampling is on -- each trace should be instrumented with the code
    // that decrements the literace counter and checks whether it is
    // greater than 0. If yes, the TLEB should be flushed, otherwise it
    // should be cleared. If we don't clean up the TLEB, the dirty
    // addresses may be occasionally passed to ThreadSanitizer during the
    // next flush.
    // In the case of a trace containing a single memory operation we don't use
    // the TLEB at all, so we don't need to clean it up.
    //
    //      literace > 0 ?
    //      |            |
    //      |            |
    //      |            |
    //      |            |
    //  yes, flush    no, cleanup
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
    //  -- memset(TLEB, 0, num_mops)
    //  -- branch to the original block end.

    //  This is the final branch.
    BranchInst *CleanupTerm = BranchInst::Create(FinishBB, CleanupBB);
    // This is the memset() (or just a MOV for smaller TLEB sizes).
    // The TLEB should be cleaned up iff |useTLEB|.
    if (useTLEB) {
      vector <Value*> MSArgs;
      MSArgs.push_back(BitCastInst::CreatePointerCast(TLEB,
                                                      Int8Ptr,
                                                      "",
                                                      CleanupTerm));
      MSArgs.push_back(ConstantInt::get(Int8, 0));
      MSArgs.push_back(ConstantInt::get(PlatformInt,
                                        trace.num_mops * ArchSize / 8));
      MSArgs.push_back(ConstantInt::get(Int32, 1));
      CallInst::Create(MemSetIntrinsicFn,
                       MSArgs.begin(), MSArgs.end(),
                       "", CleanupTerm);
    }

    // Set up the flush block. It should contain:
    //  -- call to either bb_flush_current() or bb_flush_mop().
    //  -- branch to the original block end.

    // This is the final branch.
    BranchInst *FlushTerm = BranchInst::Create(FinishBB, FlushBB);
    // This is the flush.
    if (useTLEB) {
      // Call bb_flush_current(current_mops).
      assert(MopAddr == NULL);
      vector <Value*> Args(1);
      vector <Value*> idx;
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
      // TODO(glider): We'll get a mess if
      // EnableLiteRaceSampling == true and EnableTraceFlushing == false
      CallInst::Create(BBFlushCurrentFn,
                       Args.begin(), Args.end(),
                       "", FlushTerm);
    } else {
      // Call bb_flush_mop(current_mop, addr).
      vector <Value*> Args(2);
      vector <Value*> idx;
      idx.push_back(ConstantInt::get(PlatformInt, 0));
      Value *PassportPtr =
          GetElementPtrInst::Create(TracePassportGlob,
                                    idx.begin(),
                                    idx.end(),
                                    "",
                                    FlushTerm);
      Args[0] = BitCastInst::CreatePointerCast(PassportPtr, TraceInfoTypePtr,
                                               "",
                                               FlushTerm);
      Args[1] = MopAddr;
      CallInst::Create(BBFlushMop, Args.begin(), Args.end(), "", FlushTerm);
    }
  }
}

// TsanOnlineInstrument is a module pass, so this is just a helper function, not
// an interface implementation.
void TsanOnlineInstrument::runOnTrace(Trace &trace,
                                      bool first_dtor_bb) {
  TLEBIndex = 0;
  instrumentation_stats.newTrace();
  bool have_passport = makeTracePassport(trace);
  if (have_passport) {
    instrumentation_stats.newInstrumentedTrace();
    if ((trace.mops_to_instrument.size() > 1) || UseTlebForMinimalBlocks) {
      // Instrument memory operations and function calls.
      for (BlockSet::iterator TI = trace.blocks.begin(),
                              TE = trace.blocks.end();
           TI != TE; ++TI) {
        runOnBasicBlock(*TI, first_dtor_bb, trace, /*useTLEB*/true);
        first_dtor_bb = false;
      }
      // If a trace has a passport, we should be able to insert a flush call
      // before its exit points.
      assert(trace.exits.size());
      for (BlockSet::iterator EI = trace.exits.begin(),
                              EE = trace.exits.end();
           EI != EE; ++EI) {
        insertFlushCurrentCall(trace, (*EI)->getTerminator(),
                               /*useTLEB*/true, NULL);
      }
    } else {
      // The trace contains a single memory operation -- just instrument it with
      // call to bb_flush_mop() instead of passing it through TLEB.
      // TODO(glider): assert that such a trace consists of a single basic
      // block.
      for (BlockSet::iterator TI = trace.blocks.begin(),
                              TE = trace.blocks.end();
           TI != TE; ++TI) {
        runOnBasicBlock(*TI, first_dtor_bb, trace, /*useTLEB*/false);
        first_dtor_bb = false;
      }
    }
  } else {
    // Instrument only function calls in all the blocks belonging to this trace.
    for (BlockSet::iterator EI = trace.blocks.begin(),
                            EE = trace.blocks.end();
         EI != EE; ++EI) {
      for (BasicBlock::iterator BI = (*EI)->begin(), BE = (*EI)->end();
           BI != BE;
           ++BI) {
        if (isaCallOrInvoke(BI) && calls_to_instrument.count(BI)) {
          instrumentCall(BI);
        }
      }
    }
  }
}

// Find the topmost location of the instruction in the inline stack.
DILocation TsanOnlineInstrument::getTopInlinedLocation(
    BasicBlock::iterator &BI) {
  DILocation Loc(BI->getMetadata("dbg"));
  while (true) {
    DILocation Orig(Loc.getOrigLocation());
#ifdef BUILD_TSAN_FOR_OLD_LLVM
    if (Orig.getNode() != NULL) {
#else
    if (Orig.Verify()) {
#endif
      Loc = Orig;
    } else {
      break;
    }
  }
  return Loc;
}

// Note that BI is copied, not referenced.
void TsanOnlineInstrument::dumpInstructionDebugInfo(Constant *addr,
                                                    BasicBlock::iterator BI) {
  DILocation Loc = getTopInlinedLocation(BI);
  BasicBlock::iterator OldBI = BI;
  if (!Loc.getLineNumber()) {
    for (BasicBlock::iterator BE = BI->getParent()->end();
         BI != BE; ++BI) {
        Loc = getTopInlinedLocation(BI);
        if (Loc.getLineNumber()) break;
    }
  }
  if (!Loc.getLineNumber()) {
    BI = OldBI;
    Loc = getTopInlinedLocation(BI);
  }

  string file = Loc.getFilename();
  string dir = Loc.getDirectory();
  string symbol = BI->getParent()->getParent()->getNameStr();
  uintptr_t line = Loc.getLineNumber();

  debug_path_set.insert(dir);
  debug_file_set.insert(file);
  debug_symbol_set.insert(symbol);
  debug_pc_map.insert(
      make_pair(addr, DebugPcInfo(symbol, dir, file, line)));
#ifdef DEBUG_DEBUG_INFO
  errs() << symbol << ": " << dir << "/" << file << ":" << line << "\n";
#endif
}

bool TsanOnlineInstrument::isaCallOrInvoke(BasicBlock::iterator &BI) {
  return ((isa<CallInst>(BI) && (!isa<DbgDeclareInst>(BI))) ||
          isa<InvokeInst>(BI));
}

int TsanOnlineInstrument::getMopPtrSize(Value *mopPtr, bool isStore) {
  int result = ArchSize;
  const Type *mop_type = mopPtr->getType();
  if (mop_type->isSized()) {
    if (cast<PointerType>(mop_type)->getElementType()->isSized()) {
      result = TD->getTypeStoreSizeInBits(
          cast<PointerType>(mop_type)->getElementType());
    }
  }
  return result;
}

// True if any function that was inlined at this place is to be ignored
// recursively.
// TODO(glider): this seems to be too aggressive, need to check.
bool TsanOnlineInstrument::ignoreInlinedMop(BasicBlock::iterator &BI) {
  if (!IgnoreMopsByOrigin) return false;
#ifdef DEBUG_IGNORE_MOPS
  errs() << "ignoreInlinedMop: ";
  BI->dump();
#endif
  DILocation Loc(BI->getMetadata("dbg"));
  bool first = true;
  while (true) {
    DILocation OldLoc = Loc;
#ifdef BUILD_TSAN_FOR_OLD_LLVM
    DISubprogram Sub = getDISubprogram(Loc.getScope().getNode());
#else
    const MDNode *scope = (Loc.getScope());
    DISubprogram Sub = getDISubprogram(scope);
#endif
    string symbol = Sub.getLinkageName();
    string filename = Sub.getFilename();
#ifdef DEBUG_IGNORE_MOPS
    errs() << "    " << Sub.getLinkageName() << "  "
        <<  Sub.getFilename() << ":" << Sub.getLineNumber() << "\n";
#endif
    if ((first &&
         TripleVectorMatchKnown(Ignores.ignores, symbol, "", filename)) ||
        TripleVectorMatchKnown(Ignores.ignores_r, symbol, "", filename)) {
#ifdef DEBUG_IGNORE_MOPS
      errs() << "    IGNORED\n";
#endif
      instrumentation_stats.newIgnoredInlinedMop();
      return true;
    }
    first = false;
    DILocation Orig(Loc.getOrigLocation());
#ifdef BUILD_TSAN_FOR_OLD_LLVM
    if (Orig.getNode() != NULL) {
#else
    if (Orig.Verify()) {
#endif
      Loc = Orig;
    } else {
      break;
    }
  }
  return false;
}

void TsanOnlineInstrument::markMopsToInstrument(Trace &trace) {
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
        instrumentation_stats.newMop();
        if (InstrumentAll) {
          trace.mops_to_instrument.insert(BI);
          continue;
        }
        if (ignoreInlinedMop(BI)) continue;
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
              //
              // The number of instrumented accesses won't change.
              trace.mops_to_instrument.erase(LI->second);
              trace.mops_to_instrument.insert(BI);
              // But the number of uninstrumented accesses will.
              instrumentation_stats.newMopUninstrumentedByAA();

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
              if (trace.mops_to_instrument.erase(LI->second)) {
                instrumentation_stats.newMopUninstrumentedByAA();
              }
              // There cannot be other LOAD operations aliasing the
              // same location.
              break;
            }
          }
          if (!has_alias) {
            store_map[make_pair(MopPtr, size)] = BI;
            trace.mops_to_instrument.insert(BI);
          }
        } else {
          for (LocMap::iterator LI = store_map.begin(), LE = store_map.end();
               LI != LE; ++LI) {
            const pair<Value*, int> &location = LI->first;
            AliasAnalysis::AliasResult R = AA->alias(MopPtr, size,
                                                     location.first,
                                                     location.second);
            if ((R == AliasAnalysis::MustAlias) &&
                (size == location.second)) {
              // Drop the current access.
              has_alias = true;
              instrumentation_stats.newMopUninstrumentedByAA();
              // There may be other LOAD operations aliasing the same
              // location.
            }
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
            if (trace.mops_to_instrument.erase(LI->second)) {
              instrumentation_stats.newMopUninstrumentedByAA();
            }
            // It's ok to insert the same BI into mops_to_instrument twice.
            trace.mops_to_instrument.insert(BI);
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
          trace.mops_to_instrument.insert(BI);
        }
      }
    }
  }
  trace.num_mops = trace.mops_to_instrument.size();
}

bool TsanOnlineInstrument::makeTracePassport(Trace &trace) {
  Passport passport;
  bool isStore, isMop;
  int size;
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
        if (!trace.mops_to_instrument.count(BI)) continue;
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

        uint64_t mop_size_minus1 = size / 8 - 1;
        uint64_t mop_is_write_ = isStore ? 1 : 0;
        uint64_t mop_create_sblock_ = 0;
        // TODO(glider): may want to call ThreadSanitizerHandleMemoryAccess
        // instead of ThreadSanitizerHandleTrace. In this case we'll need to
        // set |create_sblock_| sometimes.
        Constant *mop_pc = getInstructionAddr(FunctionMopCount, BI,
                                              PlatformInt);
        // Manually fill the union fields.
        // TODO(glider): a packed structure should be better.
        Constant *mop =
            ConstantExpr::getAdd(
                ConstantInt::get(Int64,
                    ((((mop_size_minus1 << 1) + mop_is_write_) << 1) +
                     mop_create_sblock_)  << 58),
                mop_pc);
        passport.push_back(mop);
      }
    }
  }
  if (TraceNumMops) {
    // TODO(glider): don't create literace storage if sampling is disabled.
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
        *ThisModule,
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

    TracePassportType = ArrayType::get(MopType64, TraceNumMops);
    BBTraceInfoType = StructType::get(*ThisModuleContext,
                                PlatformInt, PlatformInt, PlatformInt,
                                LiteRaceStoragePtrType,
                                Int32,
                                TracePassportType,
                                NULL);

    vector<Constant*> trace_info;
    // num_mops_
    trace_info.push_back(ConstantInt::get(PlatformInt, TraceNumMops));
    // pc_
    BasicBlock::iterator First = trace.entry->begin();
    trace_info.push_back(getInstructionAddr(FunctionMopCountOnTrace,
                                            First, PlatformInt));
    // counter_
    trace_info.push_back(ConstantInt::get(PlatformInt, 0));
    // *literace_storage
    trace_info.push_back(LiteRaceStorageGlob);
    // storage_index
    trace_info.push_back(
        ConstantInt::get(Int32,
                         InstrumentedTraceCount % kLiteRaceStorageSize));
    // mops_
    trace_info.push_back(ConstantArray::get(TracePassportType, passport));

    TracePassportGlob = new GlobalVariable(
        *ThisModule,
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

bool TsanOnlineInstrument::instrumentMop(BasicBlock::iterator &BI,
                                         bool isStore,
                                         bool check_ident_store,
                                         Trace &trace,
                                         bool useTLEB) {
  if (!trace.mops_to_instrument.count(BI)) return false;
  if (!EnableMemoryInstrumentation) {
    instrumentation_stats.newMopUninstrumentedByFlag();
    return false;
  }
  instrumentation_stats.newInstrumentedMop();
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

  // MopAddr is calculated regardless of |useTLEB| value.
  if (useTLEB) {
    // Store the pointer into TLEB[TLEBIndex].
    vector <Value*> idx;
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
  } else {
    // Call bb_flush_mop() instead of TLEB magic.
    insertFlushCurrentCall(trace, BI, /*useTLEB*/false, MopAddr);
  }
  return true;
}

// Instrument llvm.memcpy and llvm.memmove.
void TsanOnlineInstrument::instrumentMemTransfer(BasicBlock::iterator &BI) {
  if (!EnableMemoryInstrumentation) return;
  MemTransferInst &IN = static_cast<MemTransferInst&>(*BI);
  vector <Value*> arg(3);
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

// Before each call/invoke instruction we update the shadow stack top with the
// current program location (PC before the call).
void TsanOnlineInstrument::instrumentCall(BasicBlock::iterator &BI) {
  // TODO(glider): should we somehow distinguish the addresses of mops and
  // calls?
  FunctionMopCount++;
  vector <Value*> end_idx;
  end_idx.push_back(ConstantInt::get(PlatformInt, 0));
  end_idx.push_back(ConstantInt::get(Int32, 0));
  // TODO(glider): can we avoid getting the element pointer twice?

  Value *StackEndPtr =
      GetElementPtrInst::Create(ShadowStack,
                                end_idx.begin(), end_idx.end(),
                                "", BI);
  Value *StackEnd = new LoadInst(StackEndPtr, "", BI);
  vector <Value*> back_idx;
  back_idx.push_back(ConstantInt::get(PlatformInt, -1));
  Value *StackBack = GetElementPtrInst::Create(StackEnd,
                                               back_idx.begin(), back_idx.end(),
                                               "", BI);

  new StoreInst(getInstructionAddr(FunctionMopCount, BI, PlatformInt),
                StackBack, BI);
}

// This method is ran only for basic blocks belonging to traces that are to be
// instrumented. Note that a single basic block shouldn't necessarily be
// instrumented.
// TsanOnlineInstrument is a module pass, so this is just a helper function, not
// an interface implementation.
void TsanOnlineInstrument::runOnBasicBlock(BasicBlock *BB,
                                           bool first_dtor_bb,
                                           Trace &trace,
                                           bool useTLEB) {
  bool is_instrumented = false;
  for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
       BI != BE;
       ++BI) {
    if (isaCallOrInvoke(BI)) {
      // Calls should be instrumented regardless of |useTLEB|.
      instrumentCall(BI);
    }
  }
  // We need two separate passes for calls and mops instrumentation,
  // because in the case of useTLEB==false a basic block can be splitted (for
  // the purpose of sampling) and the iterators will be invalidated.
  for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
       BI != BE;
       ++BI) {
    if (isa<LoadInst>(BI)) {
      // Instrument LOAD.
      bool inst_result = instrumentMop(BI, false, first_dtor_bb, trace, useTLEB);
      is_instrumented = is_instrumented || inst_result;
      if ((!useTLEB) && is_instrumented) break;
    }
    if (isa<StoreInst>(BI)) {
      // Instrument STORE.
      bool inst_result = instrumentMop(BI, true, first_dtor_bb, trace, useTLEB);
      is_instrumented = is_instrumented || inst_result;
      if ((!useTLEB) && is_instrumented) break;
    }
  }
  if (is_instrumented) instrumentation_stats.newInstrumentedBasicBlock();
}

// TODO(glider): we may need to require additional passes to run.
void TsanOnlineInstrument::getAnalysisUsage(AnalysisUsage &AU) const {
  AU.addRequired<TargetData>();
  AU.addRequired<AliasAnalysis>();
}

void TsanOnlineInstrument::parseIgnoreFile(string &file) {
  string ignore_contents = ReadFileToString(file, /*die_if_failed*/true);
  ReadIgnoresFromString(ignore_contents, &Ignores);
}

bool TsanOnlineInstrument::shouldIgnoreFunction(Function &F) {
  DILocation Loc(F.begin()->begin()->getMetadata("dbg"));
  string filename = Loc.getFilename();
  string symbol = F.getNameStr();
  return TripleVectorMatchKnown(Ignores.ignores, symbol, "", filename);
}

bool TsanOnlineInstrument::shouldIgnoreFunctionRecursively(Function &F) {
  DILocation Loc(F.begin()->begin()->getMetadata("dbg"));
  string filename = Loc.getFilename();
  string symbol = F.getNameStr();
  return TripleVectorMatchKnown(Ignores.ignores_r, symbol, "", filename);
}

// }}}
}  // namespace

// InstrumentationStats implementation {{{1
InstrumentationStats::InstrumentationStats() {
  num_functions = 0;
  num_traces = 0;
  num_bbs = 0;
  num_inst_bbs = 0;
  num_inst_traces = 0;
  num_inst_bbs_in_trace = -1;
  num_inst_traces_in_function = -1;
  traces_bbs.clear();
  traces_mops.clear();
  num_inst_mops = 0;
  num_mops = 0;
  
  // Ok to init statistics with -1.
  med_trace_size_bbs = -1;
  med_trace_size_mops = -1;
  max_trace_size_bbs = -1;
  max_trace_size_mops = -1;

  num_uninst_mops = 0;
  num_uninst_mops_aa = 0;
  num_uninst_mops_flag = 0;
  num_uninst_mops_ignored = 0;
  for (int i = 0; i < kNumStats; i++) {
    num_traces_with_n_inst_bbs[i] = 0;
  }
}

void InstrumentationStats::newFunction() {
  num_functions++;
  if (num_inst_traces_in_function > 0) {
    // TODO(glider): see below. 
  }
  num_inst_traces_in_function = 0;
}

void InstrumentationStats::newTrace() {
  num_traces++;
  if (num_inst_bbs_in_trace > 0) {
    traces_bbs.push_back(num_inst_bbs_in_trace);
    traces_mops.push_back(num_inst_mops_in_trace);
    max_trace_size_bbs = (max_trace_size_bbs > num_inst_bbs_in_trace)?
        max_trace_size_bbs : num_inst_bbs_in_trace;
    max_trace_size_mops = (max_trace_size_mops > num_inst_mops_in_trace)?
        max_trace_size_mops : num_inst_mops_in_trace;
  }
  if (num_inst_bbs_in_trace > -1) {
    int index = kNumStats - 1;
    index = (num_inst_bbs_in_trace >= index) ?
        index : num_inst_bbs_in_trace;
    num_traces_with_n_inst_bbs[index]++;
  }
  num_inst_bbs_in_trace = 0;
  num_inst_mops_in_trace = 0;
}

void InstrumentationStats::newBasicBlocks(int num) {
  num_bbs += num;
}

void InstrumentationStats::newInstrumentedTrace() {
  num_inst_traces++;
  num_inst_traces_in_function++;
}

void InstrumentationStats::newInstrumentedBasicBlock() {
  num_inst_bbs++;
  num_inst_bbs_in_trace++;
}

void InstrumentationStats::newMop() {
  num_mops++;
}

void InstrumentationStats::newInstrumentedMop() {
  num_inst_mops++;
  num_inst_mops_in_trace++;
}

void InstrumentationStats::newIgnoredInlinedMop() {
  num_uninst_mops++;
  num_uninst_mops_ignored++;
}

void InstrumentationStats::newMopUninstrumentedByAA() {
  num_uninst_mops++;
  num_uninst_mops_aa++;
}

void InstrumentationStats::newMopUninstrumentedByFlag() {
  num_uninst_mops++;
  num_uninst_mops_flag++;
}

void InstrumentationStats::finalize() {
  if (num_inst_traces_in_function) {
    // TODO(glider)
  }
  if (num_inst_bbs_in_trace) {
    max_trace_size_bbs = (max_trace_size_bbs > num_inst_bbs_in_trace)?
        max_trace_size_bbs : num_inst_bbs_in_trace;
    max_trace_size_mops = (max_trace_size_mops > num_inst_mops_in_trace)?
        max_trace_size_mops : num_inst_mops_in_trace;
    traces_bbs.push_back(num_inst_bbs_in_trace);
    traces_mops.push_back(num_inst_mops_in_trace);
  }
  int index = kNumStats - 1;
  index = (num_inst_bbs_in_trace >= index) ?
      index : num_inst_bbs_in_trace;
  num_traces_with_n_inst_bbs[index]++;
  num_inst_bbs_in_trace = 0;
  num_inst_mops_in_trace = 0;

  // Update the medians.
  sort(traces_bbs.begin(), traces_bbs.end());
  if (traces_bbs.size()) {
    med_trace_size_bbs = traces_bbs[traces_bbs.size() / 2];
  }
  sort(traces_mops.begin(), traces_mops.end());
  if (traces_mops.size()) {
    med_trace_size_mops = traces_mops[traces_mops.size() / 2];
  }
}

void InstrumentationStats::printStats() {
  finalize();
  errs() << "\n  INSTRUMENTATION STATS\n\n";
  errs() << "# of functions in the module: " << num_functions << "\n";
  errs() << "# of traces in the module: " << num_traces << "\n";
  errs() << "# of instrumented traces in the module: "
         << num_inst_traces << "\n";
  errs() << "median trace size: " << med_trace_size_bbs << " basic blocks, "
         << med_trace_size_mops << " memory operations\n";
  errs() << "max trace size: " << max_trace_size_bbs << " basic blocks, "
         << max_trace_size_mops << " memory operations\n";
  errs() << "# of basic blocks in the module: " << num_bbs << "\n";
  errs() << "# of instrumented basic blocks in the module: "
         << num_inst_bbs << "\n";
  errs() << "# of memory operations in the module: " << num_mops << "\n";
  errs() << "# of instrumented memory operations in the module: "
         << num_inst_mops << "\n";
  errs() << "Total # of uninstrumented mops in the module: "
         << num_uninst_mops << ", including: \n";
  errs() << "  # of mops ignored (with --ignore): "
         << num_uninst_mops_ignored << "\n";
  errs() << "  # of aliasing mops in the same trace: "
         << num_uninst_mops_aa << "\n";
  errs() << "  # of mops ignored because of "
            "-enable-memory-instrumentation=false: "
         << num_uninst_mops_aa << "\n";

  // Buckets.
  errs() << "\n";
  int num_traces_in_buckets = 0, num_inst_bbs_in_buckets = 0;
  for (int i = 0; i < kNumStats; i++) {
    errs() << "# of traces with " << i << " instrumented basic blocks: " <<
      num_traces_with_n_inst_bbs[i] << "\n";
      num_traces_in_buckets += num_traces_with_n_inst_bbs[i];
      num_inst_bbs_in_buckets += i * num_traces_with_n_inst_bbs[i];
  }
  assert(num_mops == num_inst_mops + num_uninst_mops);
  assert(num_uninst_mops == num_uninst_mops_aa + num_uninst_mops_ignored
                                               + num_uninst_mops_flag);
  assert(num_traces >= num_inst_traces);
  assert(num_traces == num_traces_in_buckets);
  assert(num_bbs >= num_inst_bbs);
  // TODO(glider): this is incorrect, because we have a finite number of
  // buckets.
  // assert(num_inst_bbs == num_inst_bbs_in_buckets);
}

// }}}

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
