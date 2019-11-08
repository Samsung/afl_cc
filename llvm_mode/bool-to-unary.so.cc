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

#ifdef ASSERT
# undef ASSERT
#endif
#define ASSERT(x) if (!(x)) {errs() << "ASSERT( " << #x << " ) failed at line " << __LINE__ << "\n"; exit(-1); }

#define B2U_BB_NAME "B2U."

using namespace llvm;

namespace {

  class B2UTransform : public ModulePass {

    public:

      static char ID;
      B2UTransform() : ModulePass(ID) { }

      bool runOnModule(Module &M) override;

      //void getAnalysisUsage(AnalysisUsage &Info) const;

    private:
    
      void getUsersOf(Value & V, std::vector<Instruction*> & Userlist);
      //void getUsesOf(Value & V, std::vector<Instruction*> & Uselist);
      bool isSink(Value & V);
      void Instrument(std::set< Instruction * > & InstList, unsigned & counter, const char* name);

      const char* getPassName() const override {
       return "B2UTransform instrumentation";
      }

  };

}


char B2UTransform::ID = 0;

// http://aviral.lab.asu.edu/llvm-def-use-use-def-chains/
// https://stackoverflow.com/questions/35370195/llvm-difference-between-uses-and-user-in-instruction-or-value-classes
void B2UTransform::getUsersOf(Value & V, std::vector<Instruction*> & Userlist) {
  for(auto U : V.users()){  // U is of type User*
    if (auto I = dyn_cast<Instruction>(U)){
      // an instruction uses V
      Userlist.push_back(I);
    }
  }
}

bool B2UTransform::isSink(Value & V) {
  return ( isa<BranchInst>(V) || isa<PHINode>(V) || isa<ZExtInst>(V) || isa<ReturnInst>(V) || isa<SelectInst>(V) );
}

// void B2UTransform::getUsesOf(Value & V, std::vector<Instruction*> & Uselist) {
//   for(auto U : V.uses()){  // U is of type Use*
//    if (auto I = dyn_cast<Instruction>(U)){
//     // U uses V
//     Uselist.push_back(I);
//    }
//   }
//  for(Value::use_iterator i = instr->use_begin(), ie = instr->use_end(); i!=ie; ++i){
//    Value *v = *i;
//    Instruction *vi = dyn_cast_or_null(*i); ASSERT()
//    Uselist.push_back(vi);
//  }
// }

void B2UTransform::Instrument(std::set< Instruction * > & InstList, unsigned & counter, const char* name) {

  for(Instruction * II : InstList) {

    std::vector<Instruction*> users;
    getUsersOf(*II, users);
    
    ASSERT (users.size() > 0);
    Instruction * FirstUserI = users.at(0);
    if ( users.size() == 1 && isa<BranchInst>(FirstUserI) ) {
      /* The condition is used in a branch directly, no need to add an additional branch */
      ASSERT ( cast<BranchInst>(FirstUserI)->isConditional() );
      continue;
    }

    BasicBlock * ParentBB = II->getParent(); ASSERT (ParentBB);
    //StringRef ParentBBName = ParentBB->getName();
    //Twine StartName = Twine(ParentBBName == "" ? "NoName" : ParentBBName) + Twine(".") + Twine(B2U_BB_NAME) + Twine("ICmp.") + Twine(icmp_count);
    Twine StartName = Twine(B2U_BB_NAME) + Twine(name) + Twine(".") + Twine(counter);

    /* Split the original ParentBB */
    BasicBlock * TrueBB = II->getParent()->splitBasicBlock(II->getNextNode(), StartName + Twine(".True") );
    BasicBlock * FalseBB = TrueBB->splitBasicBlock(TrueBB->getFirstInsertionPt(), StartName + Twine(".False") );
    BasicBlock * EndBB = FalseBB->splitBasicBlock(FalseBB->getFirstInsertionPt(), StartName + Twine(".End") );

    /* Update the instructions ... */
    TerminatorInst * ParentBBTerm = ParentBB->getTerminator(); ASSERT (ParentBBTerm);
    TerminatorInst * TrueBBTerm = TrueBB->getTerminator(); ASSERT (TrueBBTerm);
    TerminatorInst * FalseBBTerm = FalseBB->getTerminator(); ASSERT (FalseBBTerm);

    /* Replace the TrueBB's terminator to go to EndBB */
    TrueBBTerm->setSuccessor(0, EndBB);

    /* Replace the ParentBB's unconditional branch to a conditional branch to the TrueBB */
    BranchInst * NewTrueBBBr = BranchInst::Create(TrueBB, FalseBB, II); ASSERT (NewTrueBBBr);
    ReplaceInstWithInst(ParentBBTerm, NewTrueBBBr);

    ++counter;
    
  }

}

bool B2UTransform::runOnModule(Module &M) {

  LLVMContext &C = M.getContext();

  IntegerType *Int8Ty  = IntegerType::getInt8Ty(C);
  IntegerType *Int1Ty  = IntegerType::getInt1Ty(C);
  //IntegerType *Int32Ty = IntegerType::getInt32Ty(C);

  /* Show a banner */

  char be_quiet = 0;

  if (isatty(2) && !getenv("AFL_QUIET")) {

    SAYF(cCYA "select-to-branch " cBRI VERSION cRST " by <l.simon@samsung.com>\n");

  } else be_quiet = 1;

  

  std::set< Instruction * > ICSet; /* For ICmp */
  std::set< Instruction * > BOSet; /* For BinaryOperator */

  unsigned icmp_count = 0, binop_count = 0;
  for (auto &F : M) {
    //errs() << "F:" << F.getName() << "\n";
    for (auto &BB : F) {
      // TDOO: unary
      for (auto &I : BB) {
        
        for(Value::use_iterator i = I.use_begin(), ie = I.use_end(); i!=ie; ++i){
         Value *v = *i;
         if (v->getType() == Int1Ty) {
          bool AndOrXor = false;
          if ( isa<BinaryOperator>(I) ) {
            unsigned OpCode = cast<BinaryOperator>(&I)->getOpcode();
            AndOrXor = (Instruction::Or == OpCode || Instruction::And == OpCode || Instruction::Xor == OpCode);
          } 
          bool OK = (isa<LoadInst>(I) || isa<BranchInst>(I) || 
                     isa<PHINode>(I) || isa<ReturnInst>(I) || 
                     isa<CallInst>(I) || isa<ZExtInst>(I) || 
                     isa<ICmpInst>(I) || isa<FCmpInst>(I) /*|| 
                     AndOrXor*/ );
          if (!OK) {
            errs() << "I:" << I << "\n";
            errs() << "BB:" << BB << "\n";
            errs() << "F:" << F << "\n";
          }
          ASSERT ( OK );

          // sources = icmp/fcmp, select, Phi
          // intermediary = and/or/xor
          // sink = ret, phi, select, zext, br

          // if ( isSink(I) ) {
          //   errs() << I << "\n";
          // }
         }
       }
        
      }

    }
  }


  /* Instrumentation of compares */
  Instrument(ICSet, icmp_count, "ICmp");
  
  /* Instrumentation of and/or */
  Instrument(BOSet, binop_count, "BinOp");

  /* Say something nice. */

  if (!be_quiet) {

    if (!icmp_count) WARNF("No instrumentation ICMP found.");
    else OKF("Instrumented %u BOOL.", icmp_count);

    if (!binop_count) WARNF("No instrumentation And/Or found.");
    else OKF("Instrumented %u And/Or.", binop_count);

  }

  return true;

}

// void AFLCoverage::getAnalysisUsage(AnalysisUsage &AU) const {
//   //AU.setPreservesCFG();
//   AU.addRequired<RegToMem>();
// }

static void registerAFLPass(const PassManagerBuilder &,
                            legacy::PassManagerBase &PM) {

  PM.add(new B2UTransform());
  // AU.addRequired<PassName>();
  // PassName &P = getAnalysis<PassName>();
}


static RegisterStandardPasses RegisterB2UPass(
    PassManagerBuilder::EP_OptimizerLast, registerAFLPass);

static RegisterStandardPasses RegisterB2UPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0, registerAFLPass);
