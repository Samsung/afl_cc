/*
 * Copyright 2018 Samsung
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../config.h"
#include "../debug.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <utility>
#include <set>

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "afl-llvm-pass-parent.h"

#define S2B_BB_NAME "S2B."

using namespace llvm;

namespace {

  class S2BTransform : public AFLPassParent, public ModulePass {

    public:

      static char ID;
      S2BTransform() : ModulePass(ID) { }

      bool runOnModule(Module &M) override;

      //void getAnalysisUsage(AnalysisUsage &Info) const;

    private:
      typedef std::set< SelectInst * > InstructionsSet_t;

      const char* getPassName() const override {
       return "S2BTransform instrumentation";
      }

  };

}


char S2BTransform::ID = 0;



bool S2BTransform::runOnModule(Module &M) {

  LLVMContext &C = M.getContext();

  IntegerType *Int8Ty  = IntegerType::getInt8Ty(C);
  //IntegerType *Int32Ty = IntegerType::getInt32Ty(C);

  /* Show a banner */

  char be_quiet = 0;

  if (isatty(2) && !getenv("AFL_QUIET")) {

    SAYF(cCYA "select-to-branch " cBRI VERSION cRST "\n");

  } else be_quiet = 1;

  

  InstructionsSet_t ISet;

  unsigned select_count = 0;
  for (auto &F : M) {
    //errs() << "F:" << F.getName() << "\n";
    for (auto &BB : F) {

      for (auto &I : BB) {
        if (isa<SelectInst>(I)) {
          // errs() << "parent of I:" << I.getParent()->getName() << "\n";
          // errs() << "I:" << I << "\n";
          ISet.insert(cast<SelectInst>(&I));          
        }
      }

    }
  }


  /* Instrumentation */
  for(SelectInst * SI : ISet) {
    // //errs() << "instrumented\n";
    BasicBlock * ParentBB = SI->getParent(); ASSERT (ParentBB);
    StringRef ParentBBName = ParentBB->getName();
    Twine StartName = Twine(ParentBBName == "" ? "NoName" : ParentBBName) + Twine(".") + Twine(S2B_BB_NAME) + Twine(select_count);

    Value * Cond = SI->getCondition(); ASSERT (Cond);
    Value * True = SI->getTrueValue(); ASSERT (True);
    Value * False = SI->getFalseValue(); ASSERT (False);

    /* Split the original ParentBB */
    BasicBlock * TrueBB = SI->getParent()->splitBasicBlock(SI, StartName + Twine(select_count) + Twine(".True") );
    BasicBlock * FalseBB = SI->getParent()->splitBasicBlock(SI, StartName + Twine(select_count) + Twine(".False") );
    BasicBlock * EndBB = SI->getParent()->splitBasicBlock(SI, StartName + Twine(select_count) + Twine(".End") );

    /* Update the instructions ... */
    TerminatorInst * ParentBBTerm = ParentBB->getTerminator(); ASSERT (ParentBBTerm);
    TerminatorInst * TrueBBTerm = TrueBB->getTerminator(); ASSERT (TrueBBTerm);
    TerminatorInst * FalseBBTerm = FalseBB->getTerminator(); ASSERT (FalseBBTerm);

    /* Replace the ParentBB's unconditional branch to a conditional branch to the TrueBB */
    BranchInst * NewTrueBBBr = BranchInst::Create(TrueBB, FalseBB, Cond); ASSERT (NewTrueBBBr);
    ReplaceInstWithInst(ParentBBTerm, NewTrueBBBr);

    /* Replace the TrueBB's terminator to go to EndBB */
    TrueBBTerm->setSuccessor(0, EndBB);

    /* Note: we could stop here and it would work. Instead, I also turn the SelectInst into a PhiNode. I think it's cleaner... */
    PHINode * PHI = PHINode::Create(True->getType(), 2, StartName + Twine(".PHI")); ASSERT (PHI);
    PHI->addIncoming(True, TrueBB);
    PHI->addIncoming(False, FalseBB);    
    ReplaceInstWithInst(SI, PHI);
  
    ++select_count;
  }

  /* Say something nice. */

  if (!be_quiet) {

    if (!select_count) WARNF("No instrumentation SELECT found.");
    else OKF("Instrumented %u SELECT.", select_count);

  }

  return true;

}

// void AFLCoverage::getAnalysisUsage(AnalysisUsage &AU) const {
//   //AU.setPreservesCFG();
//   AU.addRequired<RegToMem>();
// }

static void registerAFLPass(const PassManagerBuilder &,
                            legacy::PassManagerBase &PM) {

  PM.add(new S2BTransform());
  // AU.addRequired<PassName>();
  // PassName &P = getAnalysis<PassName>();
}


static RegisterStandardPasses RegisterS2BPass(
    PassManagerBuilder::EP_OptimizerLast, registerAFLPass);

static RegisterStandardPasses RegisterS2BPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0, registerAFLPass);
