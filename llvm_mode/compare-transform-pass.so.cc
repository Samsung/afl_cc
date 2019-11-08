/*
 * Copyright 2016 laf-intel
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/ValueTracking.h"

#include <set>

using namespace llvm;

namespace {

  class CompareTransform : public ModulePass {

    public:
      static char ID;
      CompareTransform() : ModulePass(ID) {
      } 

      bool runOnModule(Module &M) override;

      const char *getPassName() const override {
        return "transforms compare functions";
      }

    private:
      bool transformCmps(Module &M, const bool processStrcmp, const bool processMemcmp);
  };
}


char CompareTransform::ID = 0;

bool CompareTransform::transformCmps(Module &M, const bool processStrcmp, const bool processMemcmp) {

  std::vector<CallInst*> calls;
  LLVMContext &C = M.getContext();
  IntegerType *Int8Ty = IntegerType::getInt8Ty(C);
  IntegerType *Int32Ty = IntegerType::getInt32Ty(C);
  IntegerType *Int64Ty = IntegerType::getInt64Ty(C);

  /* iterate over all functions, bbs and instruction and add suitable calls to strcmp/memcmp */
  for (auto &F : M) {
    for (auto &BB : F) {
      for(auto &IN: BB) {
        CallInst* callInst = nullptr;

        if ((callInst = dyn_cast<CallInst>(&IN))) {

          bool isStrcmp = processStrcmp;
          bool isMemcmp = processMemcmp;

          Function *Callee = callInst->getCalledFunction();
          if (!Callee)
            continue;
          if (callInst->getCallingConv() != llvm::CallingConv::C)
            continue;
          StringRef FuncName = Callee->getName();
          isStrcmp &= !FuncName.compare(StringRef("strcmp"));
          isMemcmp &= !FuncName.compare(StringRef("memcmp"));

          /* Verify the strcmp/memcmp function prototype */
          FunctionType *FT = Callee->getFunctionType();
          isStrcmp &= FT->getNumParams() == 2 &&
                      FT->getReturnType()->isIntegerTy(32) && 
                      FT->getParamType(0) == FT->getParamType(1) && 
                      FT->getParamType(0) == IntegerType::getInt8PtrTy(M.getContext());
          isMemcmp &= FT->getNumParams() == 3 && 
                      FT->getParamType(0)->isPointerTy() &&
                      FT->getParamType(1)->isPointerTy() &&
                      FT->getReturnType()->isIntegerTy(32);

          if (!isStrcmp && !isMemcmp)
            continue;

          /* is a strcmp/memcmp, check is we have strcmp(x, "const") or strcmp("const", x)
           * memcmp(x, "const", ..) or memcmp("const", x, ..) */
          Value *Str1P = callInst->getArgOperand(0), *Str2P = callInst->getArgOperand(1);
          StringRef Str1, Str2;
          bool HasStr1 = getConstantStringInfo(Str1P, Str1);
          bool HasStr2 = getConstantStringInfo(Str2P, Str2);

          /* one string const, one string variable */
          if (!(HasStr1 ^ HasStr2))
            continue;

          if (isMemcmp) {
            /* check if third operand is a constant integer
             * strlen("constStr") and sizeof() are treated as constant */
            Value *op2 = callInst->getArgOperand(2);
            ConstantInt* ilen = dyn_cast<ConstantInt>(op2);
            if (!ilen)
              continue;
            /* final precaution: if size of compare is larger than constant string skip it*/
            uint64_t literalLenght = HasStr1 ? GetStringLength(Str1P) : GetStringLength(Str2P);
            if (literalLenght < ilen->getZExtValue())
              continue;
          }

          calls.push_back(callInst);
        }
      }
    }
  }

  if (!calls.size())
    return false;
  errs() << "Replacing " << calls.size() << " calls to strcmp/memcmp\n";
  
  for (auto &callInst: calls) {

    Value *Str1P = callInst->getArgOperand(0), *Str2P = callInst->getArgOperand(1);
    StringRef Str1, Str2, ConstStr;
    Value *VarStr;
    bool HasStr1 = getConstantStringInfo(Str1P, Str1);
    getConstantStringInfo(Str2P, Str2);
    uint64_t constLen, memcmpLen;
    bool isMemcmp = !callInst->getCalledFunction()->getName().compare(StringRef("memcmp"));

    if (isMemcmp) {
      Value *op2 = callInst->getArgOperand(2);
      ConstantInt* ilen = dyn_cast<ConstantInt>(op2);
      memcmpLen = ilen->getZExtValue();
    }

    if (HasStr1) {
      ConstStr = Str1;
      VarStr = Str2P;
      constLen = isMemcmp ? memcmpLen : GetStringLength(Str1P);
    }
    else {
      ConstStr = Str2;
      VarStr = Str1P;
      constLen = isMemcmp ? memcmpLen : GetStringLength(Str2P);
    }

    errs() << "len " << constLen << ": " << ConstStr << "\n";

    /* split before the call instruction */
    BasicBlock *bb = callInst->getParent();
    BasicBlock *end_bb = bb->splitBasicBlock(BasicBlock::iterator(callInst));
    BasicBlock *next_bb =  BasicBlock::Create(C, "cmp_added", end_bb->getParent(), end_bb);
    BranchInst::Create(end_bb, next_bb);
    PHINode *PN = PHINode::Create(Int32Ty, constLen + 1, "cmp_phi");

    TerminatorInst *term = bb->getTerminator();
    BranchInst::Create(next_bb, bb);
    term->eraseFromParent();

    for (uint64_t i = 0; i < constLen; i++) {

      BasicBlock *cur_bb = next_bb;

      char c = ConstStr[i];

      BasicBlock::iterator IP = next_bb->getFirstInsertionPt();
      IRBuilder<> IRB(&*IP);

      Value* v = ConstantInt::get(Int64Ty, i);
      Value *ele  = IRB.CreateInBoundsGEP(VarStr, v, "empty");
      Value *load = IRB.CreateLoad(ele);
      Value *isub;
      if (HasStr1)
        isub = IRB.CreateSub(ConstantInt::get(Int8Ty, c), load);
      else
        isub = IRB.CreateSub(load, ConstantInt::get(Int8Ty, c));

      Value *sext = IRB.CreateSExt(isub, Int32Ty); 
      PN->addIncoming(sext, cur_bb);


      if (i < constLen - 1) {
        next_bb =  BasicBlock::Create(C, "cmp_added", end_bb->getParent(), end_bb);
        BranchInst::Create(end_bb, next_bb);

        TerminatorInst *term = cur_bb->getTerminator();
        Value *icmp = IRB.CreateICmpEQ(isub, ConstantInt::get(Int8Ty, 0));
        IRB.CreateCondBr(icmp, next_bb, end_bb);
        term->eraseFromParent();
      } else {
        //IRB.CreateBr(end_bb);
      }

      //add offset to varstr
      //create load
      //create signed isub
      //create icmp
      //create jcc
      //create next_bb
    }

    /* since the call is the first instruction of the bb it is save to
     * replace it with a phi instruction */
    BasicBlock::iterator ii(callInst);
    ReplaceInstWithInst(callInst->getParent()->getInstList(), ii, PN);
  }


  return true;
}

bool CompareTransform::runOnModule(Module &M) {

  llvm::errs() << "Running compare-transform-pass by laf.intel@gmail.com\n"; 
  transformCmps(M, true, true);
  verifyModule(M);

  return true;
}

static void registerCompTransPass(const PassManagerBuilder &,
                            legacy::PassManagerBase &PM) {

  auto p = new CompareTransform();
  PM.add(p);

}

static RegisterStandardPasses RegisterCompTransPass(
    PassManagerBuilder::EP_OptimizerLast, registerCompTransPass);

static RegisterStandardPasses RegisterCompTransPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0, registerCompTransPass);

