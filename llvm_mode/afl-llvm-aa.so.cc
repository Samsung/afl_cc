#define AFL_LLVM_PASS

#include "../config.h"
#include "../debug.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fstream>

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/CFG.h"

using namespace llvm;

// TODO: list of function thru a module pass

namespace {

  class AAResult : public FunctionPass {

   
    public:

      static char ID;
      AAResult() :FunctionPass(ID) { }

      bool runOnFunction(Function &F) override;

      void getAnalysisUsage(AnalysisUsage &AU) const override {
        AU.addRequired<AAResultsWrapperPass>();
        AU.setPreservesAll();
      }

      void setAliasCalleeList(LLVMContext & C, std::set<Value *> & LF); // TODO: put in utils.c
      
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      StringRef getPassName() const override {
#else
      const char* getPassName() const override {
#endif
       return "AAResult";
      }

  };

}

static inline bool isInterestingPointer(Value *V) {
  return V->getType()->isPointerTy()
      && !isa<ConstantPointerNull>(V);
}

char AAResult::ID = 0;

void AAResult::setAliasCalleeList(LLVMContext & C, std::set<Value *> & LF) {

  //TODO: utils::init_metanames();
  //+ parent

  // MDString * S = MDString::get(C, value); ASSERT (S);
  // MDNode* Meta = MDNode::get(C, S);
  // /* origin = value */
  // I.setMetadata(origin, Meta);
}

// see AliasAnalysisEvaluator.cpp
// $OPT -disable-basicaa -scev-aa -aa-eval testdict.bc
// $OPT -load ../dsa/LLVMDataStructure.so -load ../dsa/AssistDS.so  -load ../afl-llvm-aa.so -afl-aa devirt.bc -o devirt.bc
bool AAResult::runOnFunction(Function &F) {
  Module & M = *F.getParent();

  std::set<Value *> listFunctions;
  
  for (auto & FF : M) {
    if (!FF.isIntrinsic())
      listFunctions.insert(&FF);
  }
  errs() << "Function:" << F.getName() << "\n";
  AliasAnalysis &AA = getAnalysis<AAResultsWrapperPass>().getAAResults();
  for (auto & BB : F) {
    for (auto & I : BB) {
      if (auto CS = CallSite(&I)) {
        Value * Callee = CS.getCalledValue();

        if (!isa<Function>(Callee) && isInterestingPointer(Callee)) {
          errs() << Callee << ":" << CS.getInstruction() << ":" << *Callee << "\n";

          for (auto & E : listFunctions) {
            AliasResult AResult = AA.alias (Callee, E);
            switch(AResult) {
              case 0:
                errs()<<  "NoAlias with " << E->getName() << "\n";
                break;
              ///< No dependencies.
              case 1:
                errs()<<"MayAlias with " << E->getName() << "\n";    ///< Anything goes
                break;
              case 2: 
                errs()<<"PartialAlias with " << E->getName() << "\n";///< Pointers differ, but pointees overlap.
                break;

              case 3: 
                errs()<<"MustAlias with " << E->getName() << "\n";
            }
          }
        }
        
      }
    }
  }
  errs() << "here\n";
  return true;
}

static void registerAAResultPass(const PassManagerBuilder &,
                            legacy::PassManagerBase &PM) {

  PM.add(new AAResult());

}

// for opt
static RegisterPass<AAResult> X("afl-aa", "AFL AA pass",
                             false /* Only looks at CFG */,
                             false /* Analysis Pass */);

// for clang - not needed
// static RegisterStandardPasses RegisterAAResPass(
//     PassManagerBuilder::EP_OptimizerLast, registerAAResPass);

// static RegisterStandardPasses RegisterAAResPass0(
//     PassManagerBuilder::EP_EnabledOnOptLevel0, registerAAResPass);
