#define DEBUG_TYPE "mops"
#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Analysis/DebugInfo.h"
#include "llvm/Instructions.h"
#include "llvm/InstrTypes.h"
#include "llvm/IntrinsicInst.h"
#include "llvm/InlineAsm.h"
#include "llvm/CallingConv.h"
#include "llvm/Pass.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Type.h"
#include "llvm/Function.h"
#include "llvm/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/Statistic.h"

#include <stdint.h>
using namespace llvm;

#define DEBUG 0

namespace {
  typedef std::vector <Constant*> Passport;
  struct TsanOnlineInstrument : public ModulePass { // {{{1
    static char ID; // Pass identification, replacement for typeid
    int BBCount, ModuleFunctionCount, ModuleMopCount, TLEBIndex, OldTLEBIndex;
    Value *BBPassportGlob;
    int BBNumMops;
    Constant *MopFn, *BBStartFn, *BBEndFn, *BBFlushFn, *BBFlushSliceFn;
    Constant *RtnCallFn, *RtnExitFn;
    const PointerType *UIntPtr, *MopTyPtr, *Int8Ptr;
    const Type *Int32;
    const Type *Void;
    const StructType *MopTy;
    const ArrayType *BBPassportType, *BBExtPassportType;
    const Type *TLEBTy;
    const PointerType *TLEBPtrType;
    static const int kTLEBSize = 100;
    static const int kBBAddr = 1000;
    static const int kFNV1aPrime = 6733;
    int ModuleID;

    TsanOnlineInstrument() : ModulePass(&ID) { }

    uintptr_t getAddr(int bb_index, int mop_index, Instruction *dump) {
      uintptr_t result = ((ModuleID *kBBAddr) + bb_index) * kBBAddr + mop_index;
      if (dump) {
        DumpDebugInfo(result, *dump);
      }
      return result;
    }

    uintptr_t getModuleID(Module &M) {
      uintptr_t result = 0;
      char tmp;
      std::string name = M.getModuleIdentifier();
      for (size_t i = 0; i < name.size(); i++) {
        tmp = name[i];
        result = (result ^ tmp) % 512;
        result = (result * kFNV1aPrime) % 512;
      }
      return result;
    }

    virtual bool runOnModule(Module &M) {
      BBCount = 0;
      ModuleFunctionCount = 0;
      ModuleMopCount = 0;
      ModuleID = getModuleID(M);
      LLVMContext &Context = M.getContext();
      UIntPtr = Type::getInt32PtrTy(Context);
      Int32 = Type::getInt32Ty(Context);
      Int8Ptr = PointerType::get(Type::getInt8Ty(Context), 0);
      Void = Type::getVoidTy(Context);
      MopTy = StructType::get(Context, Int32, Int32, Int32, NULL);
      MopTyPtr = PointerType::get(MopTy, 0);
      BBExtPassportType = ArrayType::get(MopTy, kTLEBSize);
      TLEBTy = ArrayType::get(UIntPtr, kTLEBSize);
      TLEBPtrType = PointerType::get(UIntPtr, 0);
      MopFn = M.getOrInsertFunction("mop", Void,
                                    UIntPtr, Int32, Int32,
                                    (Type*)0);
      BBStartFn = M.getOrInsertFunction("bb_start", TLEBPtrType, (Type*)0);
      BBEndFn = M.getOrInsertFunction("bb_end",
                                      Void,
                                      MopTyPtr, Int32, (Type*)0);
      // void* bb_flush(next_mops, next_num_mops, next_bb_index)
      BBFlushFn = M.getOrInsertFunction("bb_flush",
                                        TLEBPtrType,
                                        MopTyPtr, Int32, Int32, (Type*)0);
      // void bb_flush_slice(slice_start, slice_end)
      BBFlushSliceFn = M.getOrInsertFunction("bb_flush_slice",
                                             Void,
                                             Int32, Int32, (Type*)0);
      RtnCallFn = M.getOrInsertFunction("rtn_call",
                                        Void,
                                        Int32, (Type*)0);
      RtnExitFn = M.getOrInsertFunction("rtn_exit",
                                        Void, (Type*)0);
      int nBB = 1, FnBB = 1;
      for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
        ModuleFunctionCount++;
        FnBB = nBB;
        int startBBCount = BBCount;
        bool instrument_call_ret = true;
#if DEBUG
        errs() << "F" << ModuleFunctionCount << ": " << F->getNameStr() << "\n";
#endif
        if (F->isDeclaration()) continue;
        //Function *FunPtr = F;
        if ((F->getNameStr()).find("_Znw") != std::string::npos) {
          continue;
        }
        if ((F->getNameStr()).find("_Zdl") != std::string::npos) {
          continue;
        }
        for (Function::iterator BB = F->begin(), E = F->end(); BB != E; ++BB) {
          nBB++;
          //FunPtr = NULL;
          runOnBasicBlock(M, BB, instrument_call_ret);
        }
        if (instrument_call_ret) {
          Instruction* First = F->begin()->begin();
          std::vector<Value*> inst(1);
          // BBNumMops = 0 at the start of basic block.
          inst[0] = ConstantInt::get(Int32, getAddr(startBBCount, 0, First));
          CallInst::Create(RtnCallFn, inst.begin(), inst.end(), "", First);
        }
      }
      return true;
    }

    void DumpDebugInfo(uintptr_t addr, Instruction &IN) {
      DILocation Loc(IN.getMetadata("dbg"));
      std::string file = Loc.getFilename();
      std::string dir = Loc.getDirectory();
      errs() << "->";
      errs().write_hex(addr);
      errs() << "|" <<  IN.getParent()->getParent()->getNameStr() << "|" <<
                file << "|" << Loc.getLineNumber() << "|" <<
                dir << "\n";
    }

    bool ShouldFlush(Function::iterator &BB) {
      for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
           BI != BE; ++BI) {
        if (isa<LoadInst>(BI)) {
          return true;
        }
        if (isa<StoreInst>(BI)) {
          return true;
        }
        if (isa<MemCpyInst>(BI)) {
          return true;
        }
        if (isa<CallInst>(BI)) {
          return true;
        }
      }
      return false;
    }

    bool MakePassport(Module &M, Function::iterator &BB) {
      Passport passport;
      bool isStore, isMop, isMemCpy;
      int size, src_size, dest_size;
      BBNumMops = 0;
      for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
           BI != BE; ++BI) {
        isMop = false;
        isMemCpy = false;
        if (isa<LoadInst>(BI)) {
          isStore = false;
          isMop = true;
        }
        if (isa<StoreInst>(BI)) {
          isStore = true;
          isMop = true;
        }
        if (isa<MemCpyInst>(BI)) {
          isMemCpy = true;
        }
        if (isa<CallInst>(BI)) {
          // CallInst or maybe other instructions that may cause a jump.
        }
        if (isMop) {
          size = 32;
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
          mop.push_back(ConstantInt::get(Int32, getAddr(BBCount, BBNumMops, BI)));
          mop.push_back(ConstantInt::get(Int32, size/8));
          mop.push_back(ConstantInt::get(Int32, isStore));
          passport.push_back(ConstantStruct::get(MopTy, mop));
        }
        if (isMemCpy) {
          src_size = 32, dest_size = 32;
          Value *SrcPtr, *DestPtr;
          llvm::MemCpyInst &IN = static_cast<MemCpyInst&>(*BI);
          getAddr(BBCount, BBNumMops, BI);
          SrcPtr = IN.getSource();
          DestPtr = IN.getDest();
          if (SrcPtr->getType() != UIntPtr) {
            Value *Tmp = BitCastInst::CreatePointerCast(SrcPtr, UIntPtr, "", BI);
            SrcPtr = Tmp;
          }
          if (DestPtr->getType() != UIntPtr) {
            Value *Tmp = BitCastInst::CreatePointerCast(DestPtr, UIntPtr, "", BI);
            DestPtr = Tmp;
          }
          if (SrcPtr->getType()->isSized()) {
            src_size = getAnalysis<TargetData>().getTypeStoreSizeInBits(
                                                 SrcPtr->getType());
          }
          if (DestPtr->getType()->isSized()) {
            dest_size = getAnalysis<TargetData>().getTypeStoreSizeInBits(
                                                  DestPtr->getType());
          }

          std::vector<Constant*> mop(3);
          BBNumMops++;
          ModuleMopCount++;
          mop[0] = ConstantInt::get(Int32, getAddr(BBCount, BBNumMops, BI));
          mop[1] = ConstantInt::get(Int32, 0);
          mop[2] = ConstantInt::get(Int32, /*LENGTH*/2);
          passport.push_back(ConstantStruct::get(MopTy, mop));
          BBNumMops++;
          ModuleMopCount++;
          mop[0] = ConstantInt::get(Int32, getAddr(BBCount, BBNumMops, BI));
          mop[1] = ConstantInt::get(Int32, src_size);
          mop[2] = ConstantInt::get(Int32, /*LOAD*/0);
          passport.push_back(ConstantStruct::get(MopTy, mop));
          BBNumMops++;
          ModuleMopCount++;
          mop[0] = ConstantInt::get(Int32, getAddr(BBCount, BBNumMops, BI));
          mop[1] = ConstantInt::get(Int32, dest_size);
          mop[2] = ConstantInt::get(Int32, /*STORE*/1);
          passport.push_back(ConstantStruct::get(MopTy, mop));
        }
      }
      if (BBNumMops) {
        BBPassportType = ArrayType::get(MopTy, BBNumMops);
        BBPassportGlob = new GlobalVariable(
            M,
            BBPassportType,
            false,
            GlobalValue::InternalLinkage,
            ConstantArray::get(BBPassportType, passport),
            "bb_passport",
            false, 0
            );
        return true;
      }
      return false;
    }

    void InstrumentMop(BasicBlock::iterator &BI, bool isStore,
                       Value *TLEB) {
      std::vector<Value*> Args(3);
      llvm::Instruction &IN = *BI;
      if (isStore) {
        Args[0] = (static_cast<StoreInst&>(IN).getPointerOperand());
      } else {
        Args[0] = (static_cast<LoadInst&>(IN).getPointerOperand());
      }
      //Args[2] = ConstantInt::get(Int32, isStore);

      if (Args[0]->getType() == UIntPtr) {
      } else {
        Args[0] = BitCastInst::CreatePointerCast(Args[0], UIntPtr, "", BI);
      }

      //Args[1] = ConstantInt::get(Type::getInt32Ty(BI->getContext()), size/8);
      //CallInst::Create(MopFn, Args.begin(), Args.end(), "", BI);

      std::vector <Value*> idx;
      idx.push_back(ConstantInt::get(Int32, TLEBIndex));
      Value *TLEBPtr =
          GetElementPtrInst::Create(TLEB,
                                    idx.begin(),
                                    idx.end(),
                                    "",
                                    BI);
      new StoreInst(Args[0], TLEBPtr, BI);
      TLEBIndex++;
    }

    void InstrumentMemcpy(BasicBlock::iterator &BI,
                          Value *TLEB) {
      Value *MopPtr, *TLEBPtr;
      llvm::MemCpyInst &IN = static_cast<MemCpyInst&>(*BI);
      std::vector <Value*> idx(1);
      idx[0] = ConstantInt::get(Int32, TLEBIndex);
      MopPtr = IN.getLength();
      TLEBPtr =
          GetElementPtrInst::Create(TLEB,
                                    idx.begin(),
                                    idx.end(),
                                    "",
                                    BI);
      if (MopPtr->getType() == UIntPtr) {
      } else {
        MopPtr = new IntToPtrInst(MopPtr, UIntPtr, "", BI);
      }
      new StoreInst(MopPtr, TLEBPtr, BI);
      TLEBIndex++;
      idx[0] = ConstantInt::get(Int32, TLEBIndex);
      MopPtr = IN.getSource();
      TLEBPtr =
          GetElementPtrInst::Create(TLEB,
                                    idx.begin(),
                                    idx.end(),
                                    "",
                                    BI);
      if (MopPtr->getType() == UIntPtr) {
      } else {
        MopPtr = BitCastInst::CreatePointerCast(MopPtr, UIntPtr, "", BI);
      }
      new StoreInst(MopPtr, TLEBPtr, BI);
      TLEBIndex++;

      idx[0] = ConstantInt::get(Int32, TLEBIndex);
      MopPtr = IN.getDest();
      TLEBPtr =
          GetElementPtrInst::Create(TLEB,
                                    idx.begin(),
                                    idx.end(),
                                    "",
                                    BI);
      if (MopPtr->getType() == UIntPtr) {
      } else {
        MopPtr = BitCastInst::CreatePointerCast(MopPtr, UIntPtr, "", BI);
      }
      new StoreInst(MopPtr, TLEBPtr, BI);
      TLEBIndex++;
    }

    bool runOnBasicBlock(Module &M, Function::iterator &BB,
                         bool instrument_ret) {
      BBCount++;
      OldTLEBIndex = 0;
      TLEBIndex = 0;
      Value *TLEB = NULL;
      bool have_passport = MakePassport(M, BB);
      if (ShouldFlush(BB)) {
//      if (have_passport) {
        BasicBlock::iterator First = BB->begin();
#if DEBUG
        errs() << "BI: " << First->getOpcodeName() << "\n";
#endif
        std::vector <Value*> Args(3);
        std::vector <Value*> idx;
        idx.push_back(ConstantInt::get(Int32, 0));
        idx.push_back(ConstantInt::get(Int32, 0));
        if (have_passport) {
          Args[0] =
            GetElementPtrInst::Create(BBPassportGlob,
                                      idx.begin(),
                                      idx.end(),
                                      "",
                                      First);
        } else {
          Args[0] = ConstantPointerNull::get(MopTyPtr);
        }

        Args[1] = ConstantInt::get(Int32, BBNumMops);
//        if (FunPtrOrNull) {
//          Args[2] = BitCastInst::CreatePointerCast(FunPtrOrNull, Int32, "fcast", First);
//        } else {
          Args[2] = ConstantInt::get(Int32, getAddr(BBCount, 0, First));
//        }
        TLEB = CallInst::Create(BBFlushFn, Args.begin(), Args.end(), "",
                           First);
      }

#if DEBUG
      errs() << "========BB("<< BBCount<<")===========\n";
#endif

      for (BasicBlock::iterator BI = BB->begin(), BE = BB->end();
           BI != BE; ++BI) {
        bool unknown = true;  // we just don't want a bunch of nested if()s
        if (isa<LoadInst>(BI)) {
#if DEBUG
          errs() << "<";
#endif
          // Instrument LOAD.
          InstrumentMop(BI, false, TLEB);
          unknown = false;
        }
        if (isa<StoreInst>(BI)) {
#if DEBUG
          errs() << ">";
#endif
          // Instrument STORE.
          InstrumentMop(BI, true, TLEB);
          unknown = false;
        }
        if (isa<MemCpyInst>(BI)) {
          InstrumentMemcpy(BI, TLEB);
          unknown = false;
        }
        if (isa<CallInst>(BI)) {
          std::vector<Value*> inst(1);
          llvm::Instruction &IN = *BI;
          if (static_cast<CallInst&>(IN).getCalledFunction() == BBFlushFn) {
            // TODO(glider): we shouldn't encounter BBFlushFn at all.
#if DEBUG
            errs() << "BBFlushFn!\n";
#endif
            continue;
          }
          // A call invalidates the current passport ptr.
          std::vector<Value*> Args(2);
          Args[0] = ConstantInt::get(Int32, OldTLEBIndex);
          Args[1] = ConstantInt::get(Int32, TLEBIndex);
          CallInst::Create(BBFlushSliceFn, Args.begin(), Args.end(), "", BI);
          OldTLEBIndex = TLEBIndex;
          unknown = false;
        }
        if (isa<ReturnInst>(BI) && instrument_ret) {
#if DEBUG
          errs() << "-";
#endif
          std::vector<Value*> inst(0);
          CallInst::Create(RtnExitFn, inst.begin(), inst.end(), "", BI);
          unknown = false;
        }
        if (unknown) {
#if DEBUG
          errs() << " ";
#endif
        }
#if DEBUG
        errs() << "BI: " << BI->getOpcodeName() << "\n";
#endif
      }
#if DEBUG
      errs() << "========BB===========\n";
#endif
      return true;
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
