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
    const Type *Int32, *ArithmeticPtr;
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

    bool isDtor(llvm::StringRef mangled_name) {
      // TODO(glider): need a demangler here.
      if (mangled_name.find("D0") != std::string::npos) return true;
      if (mangled_name.find("D1") != std::string::npos) return true;
      return false;
    }

    virtual bool runOnModule(Module &M) {
      BBCount = 0;
      ModuleFunctionCount = 0;
      ModuleMopCount = 0;
      ModuleID = getModuleID(M);
      LLVMContext &Context = M.getContext();
      UIntPtr = Type::getInt32PtrTy(Context);
      Int32 = Type::getInt32Ty(Context);
      ArithmeticPtr = Type::getInt32Ty(Context);
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
        bool first_dtor_bb = false;
#if DEBUG
        errs() << "F" << ModuleFunctionCount << ": " << F->getName() << "\n";
#endif
        if (F->isDeclaration()) continue;
        //Function *FunPtr = F;
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
          //FunPtr = NULL;
          runOnBasicBlock(M, BB, first_dtor_bb);
          first_dtor_bb = false;
        }
        Instruction* First = F->begin()->begin();
        std::vector<Value*> inst(1);
        // BBNumMops = 0 at the start of basic block.
        inst[0] = ConstantInt::get(Int32, getAddr(startBBCount, 0, First));
        CallInst::Create(RtnCallFn, inst.begin(), inst.end(), "", First);
      }
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
          if (SrcPtr->getType()->isSized()) {
            src_size = getAnalysis<TargetData>().getTypeStoreSizeInBits(
                cast<PointerType>(SrcPtr->getType())->getElementType());
          }
          if (DestPtr->getType()->isSized()) {
            dest_size = getAnalysis<TargetData>().getTypeStoreSizeInBits(
                cast<PointerType>(DestPtr->getType())->getElementType());
          }

          if (SrcPtr->getType() != UIntPtr) {
            Value *Tmp = BitCastInst::CreatePointerCast(SrcPtr, UIntPtr, "", BI);
            SrcPtr = Tmp;
          }
          if (DestPtr->getType() != UIntPtr) {
            Value *Tmp = BitCastInst::CreatePointerCast(DestPtr, UIntPtr, "", BI);
            DestPtr = Tmp;
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
          mop[1] = ConstantInt::get(Int32, src_size/8);
          mop[2] = ConstantInt::get(Int32, /*LOAD*/0);
          passport.push_back(ConstantStruct::get(MopTy, mop));
          BBNumMops++;
          ModuleMopCount++;
          mop[0] = ConstantInt::get(Int32, getAddr(BBCount, BBNumMops, BI));
          mop[1] = ConstantInt::get(Int32, dest_size/8);
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
      idx.push_back(ConstantInt::get(Int32, TLEBIndex));
      Value *TLEBPtr =
          GetElementPtrInst::Create(TLEB,
                                    idx.begin(),
                                    idx.end(),
                                    "",
                                    BI);
      new StoreInst(MopAddr, TLEBPtr, BI);
      TLEBIndex++;
    }

    void InstrumentMemcpy(BasicBlock::iterator &BI,
                          Value *TLEB) {
      Value *MopPtr, *TLEBPtr;
      llvm::MemCpyInst &IN = static_cast<MemCpyInst&>(*BI);
      std::vector <Value*> idx(1);
      idx[0] = ConstantInt::get(Int32, TLEBIndex);
      MopPtr = new IntToPtrInst(IN.getLength(), UIntPtr, "", BI);
      TLEBPtr =
          GetElementPtrInst::Create(TLEB,
                                    idx.begin(),
                                    idx.end(),
                                    "",
                                    BI);
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
                         bool first_dtor_bb) {
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
          InstrumentMop(BI, false, TLEB, first_dtor_bb);
          unknown = false;
        }
        if (isa<StoreInst>(BI)) {
#if DEBUG
          errs() << ">";
#endif
          // Instrument STORE.
          InstrumentMop(BI, true, TLEB, first_dtor_bb);
          unknown = false;
        }
        if (isa<MemCpyInst>(BI)) {
          InstrumentMemcpy(BI, TLEB);
          unknown = false;
        }
        // TODO(glider): invoke!
        if (isa<CallInst>(BI) || isa<InvokeInst>(BI)) {
          std::vector<Value*> inst(1);
          llvm::Instruction &IN = *BI;
          if ((isa<CallInst>(BI) &&
               static_cast<CallInst&>(IN).getCalledFunction() == BBFlushFn) ||
              (isa<InvokeInst>(BI) &&
               static_cast<InvokeInst&>(IN).getCalledFunction() == BBFlushFn)) {
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
        if (isa<ReturnInst>(BI)) {
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
