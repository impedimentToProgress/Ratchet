//===-------- ConstructIdempotentRegions.cpp --------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This transformation pass is just a consumer of MemoryIdempotenceAnalysis.  It
// inserts the actual idempotence boundary instructions as intrinsics into the
// LLVM IR.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "construct-idempotent-regions"
#include "llvm/IR/Function.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Module.h"
#include "llvm/CodeGen/IdempotenceOptions.h"
#include "llvm/CodeGen/MemoryIdempotenceAnalysis.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/IR/IRBuilder.h"
#include <vector>
using namespace llvm;

class ConstructIdempotentRegions : public FunctionPass {
public:
  static char ID;  // Pass identification, replacement for typeid
  ConstructIdempotentRegions() : FunctionPass(ID) {
    initializeConstructIdempotentRegionsPass(*PassRegistry::getPassRegistry());
  }

  virtual bool runOnFunction(Function &F);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<MemoryIdempotenceAnalysis>();
    AU.addPreserved<MemoryIdempotenceAnalysis>();
    //AU.setPreservesAll();
  }
};

char ConstructIdempotentRegions::ID = 0;
INITIALIZE_PASS_BEGIN(ConstructIdempotentRegions,
    "construct-idempotent-regions",
    "Idempotent Region Construction", "true", "false")
INITIALIZE_PASS_DEPENDENCY(MemoryIdempotenceAnalysis)
INITIALIZE_PASS_END(ConstructIdempotentRegions,
    "construct-idempotent-regions",
    "Idempotent Region Construction", "true", "false")

FunctionPass *llvm::createConstructIdempotentRegionsPass() {
  return new ConstructIdempotentRegions();
}

bool ConstructIdempotentRegions::runOnFunction(Function &F) {
  assert(IdempotenceConstructionMode != IdempotenceOptions::NoConstruction &&
         "pass should not be run");

  std::string triple = F.getParent()->getTargetTriple();
  if(triple.find("arm") != std::string::npos || triple.find("thumb") != std::string::npos)
  {
    // Iterate over the analysis cut points and insert cuts.
    MemoryIdempotenceAnalysis *MIA = &getAnalysis<MemoryIdempotenceAnalysis>();
    Constant *CP = F.getParent()->getOrInsertFunction("__dummyfunc", FunctionType::get(Type::getVoidTy(F.getParent()->getContext()), false), AttributeSet());
    Function *Idem = Intrinsic::getDeclaration(F.getParent(), Intrinsic::idem);


    if(MIA->begin() != MIA->end())
    {
      CallInst::Create(CP, "", F.begin()->begin());
    }

    for (MemoryIdempotenceAnalysis::const_iterator I = MIA->begin(),
          E = MIA->end(); I != E; ++I) {
      SmallPtrSet<Instruction *, 4> Stores = MIA->CutMap_->lookup(*I);
      DEBUG(dbgs() << "JVDW: CutLocation:\t" << **I << '\n');
      DEBUG(dbgs() << "JVDW: Number Stores Cut:\t" << Stores.size() << '\n');

      IRBuilder<> Builder(*I); 

      if(IdempotenceConstructionMode == IdempotenceOptions::OptimizeForSpeed)
      {
        CallInst *CP = Builder.CreateCall(Idem, "");
      }else if(IdempotenceConstructionMode == IdempotenceOptions::OptimizeForIdeal)
      {

      }else{
        CallInst *CP = Builder.CreateCall(Idem, "");
      }
    }
  }
  return true;
}


