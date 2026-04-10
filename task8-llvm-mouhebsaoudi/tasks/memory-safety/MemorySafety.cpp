#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

#define DEBUG_TYPE "memory-safety"

using namespace llvm;

namespace {
    struct MemorySafetyPass : PassInfoMixin<MemorySafetyPass> {
    public:
        PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
            if (F.getName().startswith("__runtime_"))
                return PreservedAnalyses::all();

            errs() << "Running MemorySafetyPass on function " << F.getName() << "\n";

            Module *M = F.getParent();
            const DataLayout &DL = M->getDataLayout();
            LLVMContext &Ctx = M->getContext();

            Type *VoidTy    = Type::getVoidTy(Ctx);
            Type *Int64Ty   = Type::getInt64Ty(Ctx);
            Type *Int8PtrTy = Type::getInt8PtrTy(Ctx);

            FunctionType *MallocTy =
                    FunctionType::get(Int8PtrTy, {Int64Ty}, false);
            FunctionType *FreeTy =
                    FunctionType::get(VoidTy, {Int8PtrTy}, false);
            FunctionType *CheckTy =
                    FunctionType::get(VoidTy, {Int8PtrTy, Int64Ty}, false);
            FunctionType *StackRegTy =
                    FunctionType::get(VoidTy, {Int8PtrTy, Int64Ty}, false);
            FunctionType *StackUnregTy =
                    FunctionType::get(VoidTy, {Int8PtrTy}, false);

            FunctionCallee RuntimeMalloc =
                    M->getOrInsertFunction("__runtime_malloc", MallocTy);
            FunctionCallee RuntimeFree =
                    M->getOrInsertFunction("__runtime_free", FreeTy);
            FunctionCallee RuntimeCheck =
                    M->getOrInsertFunction("__runtime_check_addr", CheckTy);
            FunctionCallee RuntimeStackReg =
                    M->getOrInsertFunction("__runtime_register_stack", StackRegTy);
            FunctionCallee RuntimeStackUnreg =
                    M->getOrInsertFunction("__runtime_unregister_stack", StackUnregTy);

            bool Changed = false;
            SmallVector<Instruction *, 16> ToErase;
            SmallVector<AllocaInst *, 16> TrackedAllocas;

            // 1) Track stack slots (allocas) with constant size and register them
            for (BasicBlock &BB : F) {
                for (Instruction &I : BB) {
                    auto *AI = dyn_cast<AllocaInst>(&I);
                    if (!AI)
                        continue;

                    auto *ArraySizeConst =
                            dyn_cast<ConstantInt>(AI->getArraySize());
                    if (!ArraySizeConst)
                        continue; // skip variable-length allocas

                    uint64_t Count = ArraySizeConst->getZExtValue();
                    if (Count == 0)
                        continue;

                    uint64_t ElemSize =
                            DL.getTypeAllocSize(AI->getAllocatedType());
                    uint64_t Total = ElemSize * Count;

                    Instruction *InsertPt = AI->getNextNode();
                    if (!InsertPt)
                        continue;

                    IRBuilder<> B(InsertPt);
                    Value *Ptr    = B.CreateBitCast(AI, Int8PtrTy);
                    Value *SizeVal = ConstantInt::get(Int64Ty, Total);
                    B.CreateCall(RuntimeStackReg, {Ptr, SizeVal});

                    TrackedAllocas.push_back(AI);
                    Changed = true;
                }
            }

            // 2) Unregister stack slots before each return
            if (!TrackedAllocas.empty()) {
                for (BasicBlock &BB : F) {
                    if (auto *RI = dyn_cast<ReturnInst>(BB.getTerminator())) {
                        IRBuilder<> B(RI);
                        for (AllocaInst *AI : TrackedAllocas) {
                            Value *Ptr = B.CreateBitCast(AI, Int8PtrTy);
                            B.CreateCall(RuntimeStackUnreg, {Ptr});
                        }
                        Changed = true;
                    }
                }
            }

            // 3) Instrument loads, stores, malloc, free
            for (BasicBlock &BB : F) {
                for (auto It = BB.begin(); It != BB.end(); ) {
                    Instruction *I = &*It++;

                    if (auto *LI = dyn_cast<LoadInst>(I)) {
                        IRBuilder<> B(LI);
                        Value *Ptr = LI->getPointerOperand();
                        Value *CastPtr = B.CreateBitCast(Ptr, Int8PtrTy);
                        uint64_t Size = DL.getTypeStoreSize(LI->getType());
                        Value *SizeVal = ConstantInt::get(Int64Ty, Size);
                        B.CreateCall(RuntimeCheck, {CastPtr, SizeVal});
                        Changed = true;
                    } else if (auto *SI = dyn_cast<StoreInst>(I)) {
                        IRBuilder<> B(SI);
                        Value *Ptr = SI->getPointerOperand();
                        Value *CastPtr = B.CreateBitCast(Ptr, Int8PtrTy);
                        uint64_t Size =
                                DL.getTypeStoreSize(SI->getValueOperand()->getType());
                        Value *SizeVal = ConstantInt::get(Int64Ty, Size);
                        B.CreateCall(RuntimeCheck, {CastPtr, SizeVal});
                        Changed = true;
                    } else if (auto *CB = dyn_cast<CallBase>(I)) {
                        Function *Callee = CB->getCalledFunction();
                        if (!Callee)
                            continue;

                        StringRef Name = Callee->getName();

                        if (Name == "malloc") {
                            IRBuilder<> B(CB);
                            Value *SizeArg = CB->getArgOperand(0);
                            if (SizeArg->getType() != Int64Ty)
                                SizeArg = B.CreateZExtOrTrunc(SizeArg, Int64Ty);

                            Value *NewCall = B.CreateCall(RuntimeMalloc, {SizeArg});
                            if (NewCall->getType() != CB->getType())
                                NewCall = B.CreateBitCast(NewCall, CB->getType());

                            CB->replaceAllUsesWith(NewCall);
                            ToErase.push_back(CB);
                            Changed = true;
                        } else if (Name == "free") {
                            IRBuilder<> B(CB);
                            Value *PtrArg = CB->getArgOperand(0);
                            Value *CastPtr = B.CreateBitCast(PtrArg, Int8PtrTy);
                            B.CreateCall(RuntimeFree, {CastPtr});
                            ToErase.push_back(CB);
                            Changed = true;
                        }
                    }
                }
            }

            for (Instruction *I : ToErase)
                I->eraseFromParent();

            if (!Changed)
                return PreservedAnalyses::all();
            return PreservedAnalyses::none();
        }

        static bool isRequired() { return true; }
    };
} // namespace

PassPluginLibraryInfo getPassPluginInfo() {
    const auto callback = [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
                [](StringRef Name, FunctionPassManager &FPM, auto) {
                    if (Name == "memory-safety") {
                        FPM.addPass(MemorySafetyPass());
                        return true;
                    }
                    return false;
                });
    };
    return {LLVM_PLUGIN_API_VERSION, "MemorySafetyPass",
            LLVM_VERSION_STRING, callback};
};

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
    return getPassPluginInfo();
}

#undef DEBUG_TYPE
