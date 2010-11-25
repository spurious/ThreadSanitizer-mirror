#define DEBUG_TYPE "tsan"
#include "llvm/ADT/Statistic.h"
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

#define DEBUG 0

static cl::opt<std::string>
TargetArch("arch", cl::desc("Target arch: x86 or x64"));

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

  struct TsanOnlineInstrument : public ModulePass { // {{{1
    static char ID; // Pass identification, replacement for typeid
    int BBCount, ModuleFunctionCount, ModuleMopCount, TLEBIndex, OldTLEBIndex;
    Value *BBPassportGlob;
    int BBNumMops;
    Constant *BBFlushFn;
    Constant *RtnCallFn, *RtnExitFn;
    Constant *MemCpyFn, *MemMoveFn;
    const PointerType *UIntPtr, *MopTyPtr, *Int8Ptr, *TraceInfoTypePtr;
    const Type *PlatformInt, *Int8, *ArithmeticPtr, *Int32;
    const Type *Void;
    const StructType *MopTy, *TraceInfoType, *BBTraceInfoType;
    const ArrayType *BBPassportType, *BBExtPassportType, *MopArrayType;
    const Type *TLEBTy;
    const PointerType *TLEBPtrType;
    static const int kTLEBSize = 100;
    // TODO(glider): hashing constants and BB addresses should be different on
    // x86 and x86-64.
    static const int kBBHiAddr = 5000, kBBLoAddr = 100;
    static const int kFNV1aPrime = 6733, kFNV1aModulo = 2048;
    static const int kMaxAddr = 1 << 30;
    static const int kDebugInfoMagicNumber = 0xdb914f0;
    int arch_size_;
    int ModuleID;
    set<string> debug_symbol_set;
    set<string> debug_file_set;
    set<string> debug_path_set;
    map<uintptr_t, DebugPcInfo> debug_pc_map;

    TsanOnlineInstrument() : ModulePass(&ID) {
      if (TargetArch == "x86-64") {
        arch_size_ = 64;
      } else {
        arch_size_ = 32;
      }
    }

    uintptr_t getAddr(int bb_index, int mop_index, Instruction *dump) {
      uintptr_t result = ((ModuleID * kBBHiAddr) + bb_index) * kBBLoAddr + mop_index;
      if (dump) {
        DumpDebugInfo(result, *dump);
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
      // TODO(glider): need a demangler here.
      if (mangled_name.find("D0") != std::string::npos) return true;
      if (mangled_name.find("D1") != std::string::npos) return true;
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

    virtual bool runOnModule(Module &M) {
      BBCount = 0;
      ModuleFunctionCount = 0;
      ModuleMopCount = 0;
      ModuleID = getModuleID(M);
      LLVMContext &Context = M.getContext();
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
      Int8Ptr = PointerType::get(Type::getInt8Ty(Context), 0);
      Void = Type::getVoidTy(Context);
      MopTy = StructType::get(Context, PlatformInt, Int8, Int8, NULL);
      MopTyPtr = PointerType::get(MopTy, 0);
      MopArrayType = ArrayType::get(MopTy, kTLEBSize);


      // TraceInfoType represents the following class declared in
      // ts_trace_info.h:
      // class TraceInfoPOD {
      //  protected:
      //   size_t n_mops_;
      //   size_t pc_;
      //   size_t id_;
      //   size_t counter_;
      //   bool   generate_segments_;
      //   MopInfo mops_[1];
      // };
      TraceInfoType = StructType::get(Context,
                                    PlatformInt, PlatformInt, PlatformInt, PlatformInt, Int8,
                                    MopArrayType,
                                    NULL);
      TraceInfoTypePtr = PointerType::get(TraceInfoType, 0);

      TLEBTy = ArrayType::get(UIntPtr, kTLEBSize);
      TLEBPtrType = PointerType::get(UIntPtr, 0);

      // void* bb_flush(next_mops)
      BBFlushFn = M.getOrInsertFunction("bb_flush",
                                        TLEBPtrType,
                                        TraceInfoTypePtr, (Type*)0);
      //cast<Function>(BBFlushFn)->setLinkage(Function::ExternalWeakLinkage);

      // void rtn_call(void *addr)
      RtnCallFn = M.getOrInsertFunction("rtn_call",
                                        Void,
                                        PlatformInt, (Type*)0);
      //cast<Function>(RtnCallFn)->setLinkage(Function::ExternalWeakLinkage);

      // void rtn_exit()
      RtnExitFn = M.getOrInsertFunction("rtn_exit",
                                        Void, (Type*)0);
      //cast<Function>(RtnExitFn)->setLinkage(Function::ExternalWeakLinkage);

      MemCpyFn = M.getOrInsertFunction("rtl_memcpy",
                                       UIntPtr,
                                       UIntPtr, UIntPtr, PlatformInt, (Type*)0);
      //cast<Function>(MemCpyFn)->setLinkage(Function::ExternalWeakLinkage);
      MemMoveFn = M.getOrInsertFunction("rtl_memmove",
                                       UIntPtr,
                                       UIntPtr, UIntPtr, PlatformInt, (Type*)0);
      //cast<Function>(MemMoveFn)->setLinkage(Function::ExternalWeakLinkage);


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
          }
        }
      }

      int nBB = 1, FnBB = 1;
      for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
        ModuleFunctionCount++;
        FnBB = nBB;
        int startBBCount = BBCount;
        bool first_dtor_bb = false;

        if (F->isDeclaration()) continue;

        // TODO(glider): document this.
        if ((F->getName()).find("_Znw") != std::string::npos) {
          continue;
        }
        if ((F->getName()).find("_Zdl") != std::string::npos) {
          continue;
        }
        if (isDtor(F->getName())) first_dtor_bb = true;

        for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
          nBB++;
          runOnBasicBlock(M, BB, first_dtor_bb);
          first_dtor_bb = false;
        }
        Instruction* First = F->begin()->begin();
        std::vector<Value*> inst(1);
        // BBNumMops = 0 at the start of basic block.
        inst[0] = ConstantInt::get(PlatformInt, getAddr(startBBCount, 0, First));
        CallInst::Create(RtnCallFn, inst.begin(), inst.end(), "", First);
      }
      writeDebugInfo(M);

      return true;
    }

    void DumpDebugInfo(uintptr_t addr, Instruction &IN) {
      DILocation Loc(IN.getMetadata("dbg"));
      std::string file = Loc.getFilename();
      std::string dir = Loc.getDirectory();
      errs() << "->";
      errs().write_hex(addr);
      errs() << "|" <<  IN.getParent()->getParent()->getName() << "|" <<
                file << "|" << Loc.getLineNumber() << "|" <<
                dir << "\n";
      debug_path_set.insert(dir);
      debug_file_set.insert(file);
      debug_symbol_set.insert(IN.getParent()->getParent()->getName());
      debug_pc_map.insert(
          make_pair(addr, DebugPcInfo(IN.getParent()->getParent()->getName(),
                                      dir, file,
                                      Loc.getLineNumber())));
    }
    bool isaCallOrInvoke(BasicBlock::iterator &BI) {
      return ((isa<CallInst>(BI) && (!isa<DbgDeclareInst>(BI))) ||
              isa<InvokeInst>(BI));
    }


    bool ShouldFlushSlice(BasicBlock::iterator &Begin,
                          BasicBlock::iterator &End) {
      for (BasicBlock::iterator BI = Begin, BE = End;
           BI != BE; ++BI) {
#if DEBUG
        BI->dump();
#endif
        if (isa<LoadInst>(BI)) {
          return true;
        }
        if (isa<StoreInst>(BI)) {
          return true;
        }
        if (isaCallOrInvoke(BI)) {
          return true;
        }
      }
      return false;
    }


    bool MakePassportFromSlice(Module &M,
                               BasicBlock::iterator &Begin,
                               BasicBlock::iterator &End) {
      Passport passport;
      bool isStore, isMop;
      int size, src_size, dest_size;
      BBNumMops = 0;
      for (BasicBlock::iterator BI = Begin, BE = End;
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
          assert(false);
        }
        if (isMop) {
          size = getArchSize();
          BBNumMops++;
          ModuleMopCount++;
          Value *MopPtr;
          llvm::Instruction &IN = *BI;
          getAddr(BBCount, BBNumMops, BI);
          if (isStore) {
            MopPtr = (static_cast<StoreInst&>(IN).getPointerOperand());
          } else {
            MopPtr = (static_cast<LoadInst&>(IN).getPointerOperand());
          }
          if (MopPtr->getType()->isSized()) {
            if
              (cast<PointerType>(MopPtr->getType())->getElementType()->isSized())
                {
                  size =
                    getAnalysis<TargetData>().getTypeStoreSizeInBits(
                      cast<PointerType>(MopPtr->getType())->getElementType()
                    );
                }
          }


          if (MopPtr->getType() != UIntPtr) {
            MopPtr = BitCastInst::CreatePointerCast(MopPtr, UIntPtr, "", BI);
          }

          std::vector<Constant*> mop;
          mop.push_back(ConstantInt::get(PlatformInt, getAddr(BBCount, BBNumMops, BI)));
          mop.push_back(ConstantInt::get(Int8, size / 8));
          mop.push_back(ConstantInt::get(Int8, isStore));
          passport.push_back(ConstantStruct::get(MopTy, mop));
        }
      }
      if (BBNumMops) {
        BBPassportType = ArrayType::get(MopTy, BBNumMops);
        LLVMContext &Context = M.getContext();
        BBTraceInfoType = StructType::get(Context,
                                    PlatformInt, PlatformInt, PlatformInt, PlatformInt, Int8,
                                    BBPassportType,
                                    NULL);

        std::vector<Constant*> trace_info;
        // num_mops_
        trace_info.push_back(ConstantInt::get(PlatformInt, BBNumMops));
        // pc_
        trace_info.push_back(ConstantInt::get(PlatformInt, getAddr(BBCount,
                                                             0, Begin)));
        // id_ == 0 initially.
        trace_info.push_back(ConstantInt::get(PlatformInt, 0));
        // counter_
        trace_info.push_back(ConstantInt::get(PlatformInt, 0));
        // generate_segments_
        trace_info.push_back(ConstantInt::get(Int8, 1));
        // mops_
        trace_info.push_back(ConstantArray::get(BBPassportType, passport));

        BBPassportGlob = new GlobalVariable(
            M,
            BBTraceInfoType,
            false,
            GlobalValue::InternalLinkage,
            ConstantStruct::get(BBTraceInfoType, trace_info),
            "bb_passport",
            false, 0
            );
        return true;
      }
      return false;
    }

    void InstrumentMop(BasicBlock::iterator &BI, bool isStore,
                       Value *TLEB, bool check_ident_store) {
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
        // %neq      = icmp neq %old, %new
        // %neqcast  = zext %neq to i32
        // %ptr      = mul %a, %neq
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

    // Process a real basic block, i.e. a piece of code containing no calls.
    // Returns true iff a call to bb_flush was inserted.
    bool runOnBasicBlockSlice(Module &M,
                              BasicBlock::iterator &Begin,
                              BasicBlock::iterator &End,
                              bool first_dtor_bb) {
      bool result = false;
      BBCount++;
      OldTLEBIndex = 0;
      TLEBIndex = 0;
      Value *TLEB = NULL;
      bool have_passport = MakePassportFromSlice(M, Begin, End);
      if (ShouldFlushSlice(Begin, End)) {
        BasicBlock::iterator First = Begin;
#if DEBUG
        errs() << "BI: " << First->getOpcodeName() << "\n";
#endif
        std::vector <Value*> Args(1);
        std::vector <Value*> idx;
        idx.push_back(ConstantInt::get(PlatformInt, 0));
        if (have_passport) {
          Args[0] =
            GetElementPtrInst::Create(BBPassportGlob,
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
        result = true;
      }

#if DEBUG
      errs() << "========BB("<< BBCount<<")===========\n";
#endif

      for (BasicBlock::iterator BI = Begin, BE = End;
           BI != BE; ++BI) {
        bool unknown = true;  // we just don't want a bunch of nested if()s
        if (isa<LoadInst>(BI)) {
          // Instrument LOAD.
          InstrumentMop(BI, false, TLEB, first_dtor_bb);
          unknown = false;
        }
        if (isa<StoreInst>(BI)) {
          // Instrument STORE.
          InstrumentMop(BI, true, TLEB, first_dtor_bb);
          unknown = false;
        }
        // TODO(glider): invoke!
        if (isaCallOrInvoke(BI) && unknown) {
          // BBSlice can't contain a call or invoke instruction.
          errs() << "Invalid instruction: ";
          BI->dump();
          assert(false);
        }
        if (unknown) {
          // do nothing
        }
      }
#if DEBUG
      errs() << "========BB===========\n";
#endif
      return result;
    }

    bool runOnBasicBlock(Module &M, Function::iterator &BB,
                         bool first_dtor_bb) {
      BasicBlock::iterator Begin = BB->begin(), End = BB->begin();
      bool validBegin = false, validEnd = false;
      bool dirty = false, new_dirty = false;
      for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
           BI != BE;
           ++BI) {
        if (!validBegin) {
          Begin = BI;
          validBegin = true;
        }
        bool unknown = true;  // we just don't want a bunch of nested if()s
        if (isaCallOrInvoke(BI)) {
          if (isa<MemTransferInst>(BI)) {
            InstrumentMemTransfer(BI);
          }
          std::vector<Value*> inst(1);
          llvm::Instruction &IN = *BI;
          if ((isa<CallInst>(BI) &&
               static_cast<CallInst&>(IN).getCalledFunction() == BBFlushFn) ||
              (isa<InvokeInst>(BI) &&
               static_cast<InvokeInst&>(IN).getCalledFunction() == BBFlushFn)) {
            // TODO(glider): we shouldn't encounter BBFlushFn at all.
            errs() << "BBFlushFn!\n";
            assert(false);
          }
          // TODO(glider): should we invalidate first_dtor_bb after the first
          // slice?
          if (validBegin && validEnd) {
            End = BI;
            new_dirty = runOnBasicBlockSlice(M, Begin, End, first_dtor_bb);
            dirty = dirty || new_dirty;
          } else {
            if (dirty) {
              flushBeforeCall(M, BI);
            }
          }
          validBegin = false;
          validEnd = false;
          unknown = false;
        }

        if (isa<ReturnInst>(BI)) {
          if (validBegin && validEnd) {
            End = BI;
            new_dirty = runOnBasicBlockSlice(M, Begin, End, first_dtor_bb);
            dirty = dirty || new_dirty;
          }
          validBegin = false;
          validEnd = false;

          std::vector<Value*> inst(0);
          CallInst::Create(RtnExitFn, inst.begin(), inst.end(), "", BI);
          unknown = false;
        }

        if (unknown) {
          // do nothing
          End = BI;
          validEnd = true;
        }
      }
      if (validBegin) {
        End = BB->end();
        runOnBasicBlockSlice(M, Begin, End, first_dtor_bb);
      }
    }

  private:
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<TargetData>();
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
