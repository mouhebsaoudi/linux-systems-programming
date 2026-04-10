#include "llvm/ADT/SmallVector.h"
#include "llvm/Analysis/AssumptionCache.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/Local.h"
#include "llvm/Transforms/Utils/PromoteMemToReg.h"

using namespace llvm;

namespace {

static bool promoteAllocasToRegisters(Function &F) {
  bool Changed = false;

  while (true) {
    SmallVector<AllocaInst *, 16> Allocas;
    BasicBlock &Entry = F.getEntryBlock();
    if (Entry.empty())
      break;

    for (auto I = Entry.begin(), E = Entry.end(); I != E; ++I) {
      auto *AI = dyn_cast<AllocaInst>(&*I);
      if (!AI)
        continue;
      if (AI->isArrayAllocation())
        continue;
      if (!isAllocaPromotable(AI))
        continue;
      Allocas.push_back(AI);
    }

    if (Allocas.empty())
      break;

    DominatorTree DT(F);
    PromoteMemToReg(Allocas, DT);
    Changed = true;
  }

  return Changed;
}

static bool removeTriviallyDeadInstructions(Function &F) {
  SmallVector<Instruction *, 64> WorkList;

  for (BasicBlock &BB : F)
    for (Instruction &I : BB)
      if (isInstructionTriviallyDead(&I))
        WorkList.push_back(&I);

  bool Changed = false;

  while (!WorkList.empty()) {
    Instruction *I = WorkList.pop_back_val();
    if (!isInstructionTriviallyDead(I))
      continue;

    for (Use &Op : I->operands()) {
      if (auto *OpI = dyn_cast<Instruction>(Op.get()))
        if (isInstructionTriviallyDead(OpI))
          WorkList.push_back(OpI);
    }

    I->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

static bool simplifyBranches(Function &F) {
  SmallVector<BranchInst *, 16> Branches;

  for (BasicBlock &BB : F) {
    auto *BI = dyn_cast<BranchInst>(BB.getTerminator());
    if (!BI)
      continue;
    if (!BI->isConditional())
      continue;
    Branches.push_back(BI);
  }

  bool Changed = false;

  for (BranchInst *BI : Branches) {
    if (!BI->getParent())
      continue;

    BasicBlock *BB = BI->getParent();
    BasicBlock *Succ0 = BI->getSuccessor(0);
    BasicBlock *Succ1 = BI->getSuccessor(1);

    if (Succ0 == Succ1) {
      bool SuccHasPHI = false;
      for (Instruction &I : *Succ0) {
        if (!isa<PHINode>(&I))
          break;
        SuccHasPHI = true;
        break;
      }
      if (!SuccHasPHI) {
        BranchInst::Create(Succ0, BI);
        BI->eraseFromParent();
        Changed = true;
        continue;
      }
    }

    auto *CI = dyn_cast<ConstantInt>(BI->getCondition());
    if (!CI)
      continue;

    bool CondTrue = CI->isOne();
    BasicBlock *Taken = BI->getSuccessor(CondTrue ? 0 : 1);
    BasicBlock *NotTaken = BI->getSuccessor(CondTrue ? 1 : 0);

    for (Instruction &I : *NotTaken) {
      auto *PN = dyn_cast<PHINode>(&I);
      if (!PN)
        break;
      int Idx;
      while ((Idx = PN->getBasicBlockIndex(BB)) >= 0)
        PN->removeIncomingValue(Idx, false);
    }

    BranchInst::Create(Taken, BI);
    BI->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

static bool removeJumpOnlyBlocks(Function &F) {
  SmallVector<BasicBlock *, 16> Candidates;
  for (BasicBlock &BB : F)
    Candidates.push_back(&BB);

  bool Changed = false;

  for (BasicBlock *BB : Candidates) {
    if (!BB->getParent())
      continue;
    if (BB == &F.getEntryBlock())
      continue;
    if (BB->size() != 1)
      continue;

    auto *Br = dyn_cast<BranchInst>(BB->getTerminator());
    if (!Br || !Br->isUnconditional())
      continue;

    BasicBlock *Succ = Br->getSuccessor(0);
    if (Succ == BB)
      continue;

    bool SuccHasPHI = false;
    for (Instruction &I : *Succ) {
      if (!isa<PHINode>(&I))
        break;
      SuccHasPHI = true;
      break;
    }
    if (SuccHasPHI)
      continue;

    SmallVector<BasicBlock *, 8> Preds(pred_begin(BB), pred_end(BB));
    for (BasicBlock *Pred : Preds) {
      if (auto *PredBr = dyn_cast<BranchInst>(Pred->getTerminator())) {
        for (unsigned i = 0, e = PredBr->getNumSuccessors(); i != e; ++i)
          if (PredBr->getSuccessor(i) == BB)
            PredBr->setSuccessor(i, Succ);
      }
    }

    BB->eraseFromParent();
    Changed = true;
  }

  return Changed;
}

static bool mergeEntryIntoSuccessor(Function &F) {
  if (F.empty())
    return false;

  BasicBlock &Entry = F.getEntryBlock();
  auto *Br = dyn_cast<BranchInst>(Entry.getTerminator());
  if (!Br || !Br->isUnconditional())
    return false;

  BasicBlock *Succ = Br->getSuccessor(0);
  if (Succ == &Entry)
    return false;

  if (Succ->getSinglePredecessor() != &Entry)
    return false;

  bool HasPHI = false;
  for (Instruction &I : *Succ) {
    if (!isa<PHINode>(&I))
      break;
    HasPHI = true;
    break;
  }
  if (HasPHI)
    return false;

  Instruction *Term = Succ->getTerminator();
  if (!isa<ReturnInst>(Term) && !isa<UnreachableInst>(Term))
    return false;

  SmallVector<Instruction *, 16> ToMove;
  for (Instruction &I : *Succ)
    ToMove.push_back(&I);

  Instruction *InsertBefore = Br;
  for (Instruction *I : ToMove)
    I->moveBefore(InsertBefore);

  Br->eraseFromParent();
  Succ->eraseFromParent();

  return true;
}

struct DeadCodeEliminationPass : PassInfoMixin<DeadCodeEliminationPass> {
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    (void)AM;
    bool Changed = false;

    while (true) {
      bool LocalChange = false;
      LocalChange |= promoteAllocasToRegisters(F);
      LocalChange |= removeTriviallyDeadInstructions(F);
      LocalChange |= simplifyBranches(F);
      LocalChange |= removeJumpOnlyBlocks(F);
      LocalChange |= mergeEntryIntoSuccessor(F);
      LocalChange |= llvm::removeUnreachableBlocks(F);
      Changed |= LocalChange;
      if (!LocalChange)
        break;
    }

    if (!Changed)
      return PreservedAnalyses::all();

    return PreservedAnalyses::none();
  }
};

} // end anonymous namespace

extern "C" LLVM_ATTRIBUTE_WEAK PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {
      LLVM_PLUGIN_API_VERSION, "DeadCodeElimination", LLVM_VERSION_STRING,
      [](PassBuilder &PB) {
        PB.registerPipelineParsingCallback(
            [](StringRef Name, FunctionPassManager &FPM,
               ArrayRef<PassBuilder::PipelineElement>) {
              if (Name == "dead-code-elimination") {
                FPM.addPass(DeadCodeEliminationPass());
                return true;
              }
              return false;
            });
      }};
}
