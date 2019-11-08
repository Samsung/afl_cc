/*
 * Copyright 2018 Samsung Research America
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

#define AFL_LLVM_PASS

#include "../config.h"
#include "../debug.h"
#include "common.h"

#include <unistd.h>
#include <fstream>
#include <iomanip>
#include <set>

#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/Verifier.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"

#include "afl-llvm-pass-parent.h"

#define C2U_BB_NAME "C2U."
#define C2U_RECORDED "C2U.Recorded"

using namespace llvm;

#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
static LLVMContext TheContext;
#endif

namespace {

  class Compare2Unit : public AFLPassParent, public ModulePass {
  
    public:
      static char ID;
      Compare2Unit() : ModulePass(ID) { }

      bool runOnModule(Module &M) override;

#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      LLVMContext & getGlobalContext(void) {
        return TheContext;
      }
#endif

#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      StringRef getPassName() const override {
#else
      const char* getPassName() const override {
#endif
        return "simplifies and splits ICMP instructions";
      }

    private:
      void flagInstruction(Instruction &I);
      bool isFlagged(Instruction &I);
      bool recordToDictionary(Instruction & I, size_t & dictAdded, Value &V1, Value &V2, CmpInst::Predicate Pred, std::string debugInfo);
      size_t halfComparesAndRecord(Module &M, unsigned bitw, size_t & dictAdded);
      bool isStandardMagicValue(Value &V1, Value &V2, CmpInst::Predicate Pred);
      size_t simplifyCompares(Module &M);
      size_t simplifySignedness(Module &M);
      size_t getValueSizeInBits(Value &V);

  };
}

char Compare2Unit::ID = 0;

/* This function splits ICMP instructions with xGE or xLE predicates into two 
 * ICMP instructions with predicate xGT or xLT and EQ */
size_t Compare2Unit::simplifyCompares(Module &M) {
  LLVMContext &C = getGlobalContext();
  std::vector<Instruction*> icomps;
  IntegerType *Int1Ty = IntegerType::getInt1Ty(C);

  size_t Processed = 0;
  Twine StartName = Twine(C2U_BB_NAME);

  /* iterate over all functions, bbs and instruction and add
   * all integer comparisons with >= and <= predicates to the icomps vector */
  for (auto &F : M) {
    for (auto &BB : F) {
      for (auto &IN: BB) {
        CmpInst* selectcmpInst = nullptr;

        if ((selectcmpInst = dyn_cast<CmpInst>(&IN))) {

          if (selectcmpInst->getPredicate() != CmpInst::ICMP_UGE &&
              selectcmpInst->getPredicate() != CmpInst::ICMP_SGE &&
              selectcmpInst->getPredicate() != CmpInst::ICMP_ULE &&
              selectcmpInst->getPredicate() != CmpInst::ICMP_SLE ) {
            continue;
          }

          auto op0 = selectcmpInst->getOperand(0);
          auto op1 = selectcmpInst->getOperand(1);

          IntegerType* intTyOp0 = dyn_cast<IntegerType>(op0->getType());
          IntegerType* intTyOp1 = dyn_cast<IntegerType>(op1->getType());

          /* this is probably not needed but we do it anyway */
          if (!intTyOp0 || !intTyOp1) {
            continue;
          }

          /* if this is comparing special values (0,1,-1), bail out */
          if ( isStandardMagicValue(*op0, *op1, selectcmpInst->getPredicate()) ) { continue; }

          icomps.push_back(selectcmpInst);
        }
      }
    }
  }

  /* Record number of instructions we're about to process */
  Processed = icomps.size();
  for (auto &IcmpInst: icomps) {
    BasicBlock* bb = IcmpInst->getParent();

    auto op0 = IcmpInst->getOperand(0);
    auto op1 = IcmpInst->getOperand(1);

    /* find out what the new predicate is going to be */
    auto pred = dyn_cast<CmpInst>(IcmpInst)->getPredicate();
    CmpInst::Predicate new_pred;
    switch(pred) {
      case CmpInst::ICMP_UGE:
        new_pred = CmpInst::ICMP_UGT;
        break;
      case CmpInst::ICMP_SGE:
        new_pred = CmpInst::ICMP_SGT;
        break;
      case CmpInst::ICMP_ULE:
        new_pred = CmpInst::ICMP_ULT;
        break;
      case CmpInst::ICMP_SLE:
        new_pred = CmpInst::ICMP_SLT;
        break;
      default:
        ASSERT (0);
    }

    /* split before the icmp instruction */
    BasicBlock* end_bb = bb->splitBasicBlock(BasicBlock::iterator(IcmpInst));
    end_bb->setName(StartName + Twine("NewBB.ICmp"));

    /* the old bb now contains a unconditional jump to the new one (end_bb)
     * we need to delete it later */

    /* create the ICMP instruction with new_pred and add it to the old basic
     * block bb it is now at the position where the old IcmpInst was */
    Instruction* icmp_np;
    icmp_np = CmpInst::Create(Instruction::ICmp, new_pred, op0, op1, StartName + Twine("ICmp.NewPred"));
    bb->getInstList().insert(bb->getTerminator()->getIterator(), icmp_np);

    /* create a new basic block which holds the new EQ icmp */
    Instruction *icmp_eq;
    /* insert middle_bb before end_bb */
    BasicBlock* middle_bb =  BasicBlock::Create(C, StartName + Twine("InjectedMBB"),
      end_bb->getParent(), end_bb);
    icmp_eq = CmpInst::Create(Instruction::ICmp, CmpInst::ICMP_EQ, op0, op1, StartName + Twine("ICmp.ICMP_EQ"));
    middle_bb->getInstList().push_back(icmp_eq);
    /* add an unconditional branch to the end of middle_bb with destination
     * end_bb */
    BranchInst::Create(end_bb, middle_bb);

    /* replace the uncond branch with a conditional one, which depends on the
     * new_pred icmp. True goes to end, false to the middle (injected) bb */
    auto term = bb->getTerminator();
    BranchInst::Create(end_bb, middle_bb, icmp_np, bb);
    term->eraseFromParent();

    /* replace the old IcmpInst (which is the first inst in end_bb) with a PHI
     * inst to wire up the loose ends */
    PHINode *PN = PHINode::Create(Int1Ty, 2, "");
    /* the first result depends on the outcome of icmp_eq */
    PN->addIncoming(icmp_eq, middle_bb);
    /* if the source was the original bb we know that the icmp_np yielded true
     * hence we can hardcode this value */
    PN->addIncoming(ConstantInt::get(Int1Ty, 1), bb);
    /* replace the old IcmpInst with our new and shiny PHI inst */
    BasicBlock::iterator ii(IcmpInst);
    ReplaceInstWithInst(IcmpInst->getParent()->getInstList(), ii, PN);
  }

  return Processed;
}

/* this function transforms signed compares to equivalent unsigned compares */
size_t Compare2Unit::simplifySignedness(Module &M) {
  LLVMContext &C = getGlobalContext();
  std::vector<Instruction*> icomps;
  IntegerType *Int1Ty = IntegerType::getInt1Ty(C);

  size_t Processed = 0;
  Twine StartName = Twine(C2U_BB_NAME) + Twine("SimplifySign.");

  /* iterate over all functions, bbs and instruction and add
   * all signed compares to icomps vector */
  for (auto &F : M) {
    for (auto &BB : F) {
      for(auto &IN: BB) {
        CmpInst* selectcmpInst = nullptr;

        if ((selectcmpInst = dyn_cast<CmpInst>(&IN))) {

          if (selectcmpInst->getPredicate() != CmpInst::ICMP_SGT &&
             selectcmpInst->getPredicate() != CmpInst::ICMP_SLT
             ) {
            continue;
          }

          auto op0 = selectcmpInst->getOperand(0);
          auto op1 = selectcmpInst->getOperand(1);

          IntegerType* intTyOp0 = dyn_cast<IntegerType>(op0->getType());
          IntegerType* intTyOp1 = dyn_cast<IntegerType>(op1->getType());

          /* see above */
          if (!intTyOp0 || !intTyOp1) {
            continue;
          }

          /* i think this is not possible but to lazy to look it up */
          if (intTyOp0->getBitWidth() != intTyOp1->getBitWidth()) {
            continue;
          }

          if (intTyOp0->getBitWidth() > 64) {
            ASSERT (0);
          }

          /* if this is comparing special values (0,1,-1), bail out */
          if ( isStandardMagicValue(*op0, *op1, selectcmpInst->getPredicate()) ) { continue; }

          icomps.push_back(selectcmpInst);
        }
      }
    }
  }

  /* Record number of instructions we're about to process */
  Processed = icomps.size();


  for (auto &IcmpInst: icomps) {
    BasicBlock* bb = IcmpInst->getParent();

    auto op0 = IcmpInst->getOperand(0);
    auto op1 = IcmpInst->getOperand(1);

    IntegerType* intTyOp0 = dyn_cast<IntegerType>(op0->getType());
    unsigned bitw = intTyOp0->getBitWidth();
    IntegerType *IntType = IntegerType::get(C, bitw);

    /* get the new predicate */
    auto pred = dyn_cast<CmpInst>(IcmpInst)->getPredicate();
    CmpInst::Predicate new_pred;
    if (pred == CmpInst::ICMP_SGT) {
      new_pred = CmpInst::ICMP_UGT;
    } else {
      new_pred = CmpInst::ICMP_ULT;
    }

    BasicBlock* end_bb = bb->splitBasicBlock(BasicBlock::iterator(IcmpInst));
    end_bb->setName(StartName + Twine("NewBB"));

    /* create a 1 bit compare for the sign bit. to do this shift and trunc
     * the original operands so only the first bit remains.*/
    Instruction *s_op0, *t_op0, *s_op1, *t_op1, *icmp_sign_bit;

    s_op0 = BinaryOperator::Create(Instruction::LShr, op0, ConstantInt::get(IntType, bitw - 1), StartName + Twine("LShr0"));
    bb->getInstList().insert(bb->getTerminator()->getIterator(), s_op0);
    t_op0 = new TruncInst(s_op0, Int1Ty, StartName + Twine("Lower0"));
    bb->getInstList().insert(bb->getTerminator()->getIterator(), t_op0);

    s_op1 = BinaryOperator::Create(Instruction::LShr, op1, ConstantInt::get(IntType, bitw - 1), StartName + Twine("LShr1"));
    bb->getInstList().insert(bb->getTerminator()->getIterator(), s_op1);
    t_op1 = new TruncInst(s_op1, Int1Ty, StartName + Twine("Lower1"));
    bb->getInstList().insert(bb->getTerminator()->getIterator(), t_op1);

    /* compare of the sign bits */
    icmp_sign_bit = CmpInst::Create(Instruction::ICmp, CmpInst::ICMP_EQ, t_op0, t_op1, StartName + Twine("EQ01"));
    bb->getInstList().insert(bb->getTerminator()->getIterator(), icmp_sign_bit);

    /* create a new basic block which is executed if the signedness bit is
     * different */ 
    Instruction *icmp_inv_sig_cmp;
    BasicBlock* sign_bb = BasicBlock::Create(C, StartName + Twine("NewBB.SignNEq"), end_bb->getParent(), end_bb);
    if (pred == CmpInst::ICMP_SGT) {
      /* if we check for > and the op0 positiv and op1 negative then the final
       * result is true. if op0 negative and op1 pos, the cmp must result
       * in false
       */
      icmp_inv_sig_cmp = CmpInst::Create(Instruction::ICmp, CmpInst::ICMP_ULT, t_op0, t_op1, StartName + Twine("ICmp.ULT"));
    } else {
      /* just the inverse of the above statement */
      icmp_inv_sig_cmp = CmpInst::Create(Instruction::ICmp, CmpInst::ICMP_UGT, t_op0, t_op1, StartName + Twine("ICmp.UGT"));
    }
    sign_bb->getInstList().push_back(icmp_inv_sig_cmp);
    BranchInst::Create(end_bb, sign_bb);

    /* create a new bb which is executed if signedness is equal */
    Instruction *icmp_usign_cmp;
    BasicBlock* middle_bb =  BasicBlock::Create(C, StartName + Twine("NewBB.SignEq"), end_bb->getParent(), end_bb);
    /* we can do a normal unsigned compare now */
    icmp_usign_cmp = CmpInst::Create(Instruction::ICmp, new_pred, op0, op1, StartName + Twine("ICmp.NewPred"));
    middle_bb->getInstList().push_back(icmp_usign_cmp);
    BranchInst::Create(end_bb, middle_bb);

    auto term = bb->getTerminator();
    /* if the sign is eq do a normal unsigned cmp, else we have to check the
     * signedness bit */
    BranchInst::Create(middle_bb, sign_bb, icmp_sign_bit, bb);
    term->eraseFromParent();


    PHINode *PN = PHINode::Create(Int1Ty, 2, "");

    PN->addIncoming(icmp_usign_cmp, middle_bb);
    PN->addIncoming(icmp_inv_sig_cmp, sign_bb);

    BasicBlock::iterator ii(IcmpInst);
    ReplaceInstWithInst(IcmpInst->getParent()->getInstList(), ii, PN);
  }

  return Processed;
}

void Compare2Unit::flagInstruction(Instruction &I) {
  ASSERT (isa<ICmpInst>(&I));

  if ( !isFlagged(I) ) {
    
    /* Add metadata */
    IntegerType *Int1Ty = IntegerType::getInt1Ty(getGlobalContext());

#if LLVM_VERSION_CODE <= LLVM_VERSION(3, 4)
    auto Meta = MDNode::get(getGlobalContext(), ConstantInt::get(Int1Ty, 1));
#else
    // http://jiten-thakkar.com/posts/how-to-read-and-write-metadata-in-llvm
    auto Meta = MDNode::get(getGlobalContext(), ConstantAsMetadata::get(ConstantInt::get(Int1Ty, 1)));
#endif
    I.setMetadata(C2U_RECORDED, Meta);
  }
}
      
bool Compare2Unit::isFlagged(Instruction &I) {
  return (I.getMetadata(C2U_RECORDED) != NULL);
}


size_t Compare2Unit::getValueSizeInBits(Value &V) {

  if ( isa<CastInst>(&V) ) {
    CastInst * CI = cast<CastInst>(&V);
    return CI->getSrcTy()->getPrimitiveSizeInBits();
  }
  return V.getType()->getPrimitiveSizeInBits();
}

/* return value indicates if a value is present in the dictionnary at the end of thie function:
    - because it's been added, OR
    - because it was already present
 */
bool Compare2Unit::recordToDictionary(Instruction & I, size_t & dictAdded, Value &V1, Value &V2, 
                                      CmpInst::Predicate Pred, std::string debugInfo) {

  if ( !(isa<ConstantInt>(&V1) || isa<ConstantInt>(&V2)) ) { return false; }

  Value &V = isa<ConstantInt>(&V1) ? V1 : V2;

  /* Add to the dictionary */
  if ( isa<ConstantInt>(&V) ) {

    ConstantInt * CI = cast<ConstantInt>(&V);
    uint64_t Val = CI->getZExtValue();
    unsigned nBytes = CI->getBitWidth() / 8;
    ASSERT ( nBytes <= 8 && "Integers greater than 64bits not supported");

    /* Avoid special values that AFL will use anyway */
    if ( isStandardMagicValue(V1, V2, Pred) ) {
      return false;
    }

    //ASSERT ( !CI->isNegative() && "Negative integers not supported");
    
#if 0
    /* no longer used, but useful to test if -constcombine pass works as expected 
       without -constcombine, integer promotions cast is never optimized
    */
    ASSERT ( nBytes == 4 || nBytes == 8 );

    /* Get the actual size of V before Trunc/Ext 
       This is useful to avoid problems with integer promotion
       Note: it it's Trunc, nBytes will have bigger size. 
    */
    Value &OV = (&V == &V1) ? V2 : V1;
    nBytes = getValueSizeInBits(OV) / 8;
    u8 * ptr = 0;
    u8 v8 = 0;
    u16 v16 = 0;
    u32 v32 = 0;
    u64 v64 = 0;
    if ( nBytes == 1 ) {
      v8 = Val; ptr = &v8;
      ASSERT (v8 == Val);
    } else if ( nBytes == 2 ) {
      v16 = Val; ptr = (u8*)&v16;
      ASSERT (v16 == Val);
    } else if ( nBytes == 4 ) {
      v32 = Val; ptr = (u8*)&v32;
      ASSERT (v32 == Val);
    } else if ( nBytes == 8 ) {
      v64 = Val; ptr = (u8*)&v64;
      ASSERT (v64 == Val);
    } else {
      ASSERT ("Invalid size");
    }
#endif
    char buf[8]; /* Max size */
    memset(buf, 0, sizeof(buf));
    if(sys::IsBigEndianHost) {
      u8 * p = (u8*)&Val;
      for ( size_t i=0; i<8/2; ++i ) {
        uint8_t tmp = p[i];
        p[i] = p[8 - i - 1];
        p[8 - i - 1] = tmp;
      }
    }
    memcpy(buf, (u8*)&Val, nBytes);
    

    std::string KeyWord(buf, nBytes);
    utils::DictElt elmt = utils::DictElt( KeyWord, debugInfo );
    ASSERT ( !utils::isDictRecordedToInstr(I, C2U_DICT) ); 
    utils::recordDictToInstr(getGlobalContext(), I, elmt, C2U_DICT);
    ++dictAdded;
    
    /* value present in dictionary */
    return true;
  }

  return false;
}

bool Compare2Unit::isStandardMagicValue(Value &V1, Value &V2, CmpInst::Predicate Pred) {

  if ( isa<ConstantInt>(&V1) || isa<ConstantInt>(&V2) ) {
    ConstantInt &C = isa<ConstantInt>(&V1) ? *cast<ConstantInt>(&V1) : *cast<ConstantInt>(&V2);
    Value &V = (&V1 == &C) ? V2 : V1;
    // errs() << C << "\n";
    // errs() << V << "\n";
    // errs() << C.isMaxValue(false) << " " << C.isMaxValue(true) << " " << C.isMinValue(false) << " " << C.isMinValue(false) << "\n";
    /*
    C.isMaxValue(false) || C.isMaxValue(true) ||
    C.isMinValue(false) || C.isMinValue(true) ||
    C.isMinusOne()
    */

    /* if it's an (in-)equality, AFL can find 0 and 1 pretty fast */
    bool isEq = (Pred == CmpInst::ICMP_EQ || Pred == CmpInst::ICMP_NE);
    //errs() << "isEq:" << isEq << "\n";
    if ( isEq && (C.isZero() || C.isOne()) ) {
      //errs() << "true 1\n";
      return true;
    }

    /* for the rest (equality or not), return values of function (0,-1) should not be instrumented */
    if ( isa<CallInst>(&V) && (C.isZero() || C.isOne() || C.isMinusOne()) ) {
      //errs() << "true 2\n";
      return true;
    }

    if ( !isEq && (C.isZero() || C.isOne() || C.isMinusOne()) ) {
      return true;
    }
  }

  return false;
}
  


/* splits icmps of size bitw into two nested icmps with bitw/2 size each
   and/or record magic values to dictionary */
size_t Compare2Unit::halfComparesAndRecord(Module &M, unsigned bitw, size_t & dictAdded) {
  LLVMContext &C = getGlobalContext();

  IntegerType *Int1Ty = IntegerType::getInt1Ty(C);
  IntegerType *OldIntType = IntegerType::get(C, bitw);
  IntegerType *NewIntType = IntegerType::get(C, bitw / 2);

  size_t Processed = 0;
  #define StartName Twine(C2U_BB_NAME) + Twine("HalfCompare.") + Twine(std::to_string(bitw/2)) + Twine(".")
  std::vector<Instruction*> icomps;

  if ( !(bitw == 8 || bitw == 16 || bitw == 32 || bitw == 64) ) {
    ASSERT (0);
  }

  /* get all EQ, NE, UGT, and ULT icmps of width bitw. if the other two 
   * unctions were executed only these four predicates should exist */
  for (auto &F : M) {
    for (auto &BB : F) {
      for(auto &IN: BB) {
        CmpInst* selectcmpInst = nullptr;

        if ((selectcmpInst = dyn_cast<CmpInst>(&IN))) {

          if(selectcmpInst->getPredicate() != CmpInst::ICMP_EQ &&
             selectcmpInst->getPredicate() != CmpInst::ICMP_NE &&
             selectcmpInst->getPredicate() != CmpInst::ICMP_UGT &&
             selectcmpInst->getPredicate() != CmpInst::ICMP_ULT
             ) {
            continue;
          }

          auto op0 = selectcmpInst->getOperand(0);
          auto op1 = selectcmpInst->getOperand(1);

          IntegerType* intTyOp0 = dyn_cast<IntegerType>(op0->getType());
          IntegerType* intTyOp1 = dyn_cast<IntegerType>(op1->getType());

          if (!intTyOp0 || !intTyOp1) {
            continue;
          }

          /* check if the bitwidths are the one we are looking for */
          if (intTyOp0->getBitWidth() != intTyOp1->getBitWidth()) {
            ASSERT (0);
          }

          if (intTyOp0->getBitWidth() != bitw) {
            continue;
          }

          /* if this is comparing special values (0,1,-1), bail out */
          if ( isStandardMagicValue(*op0, *op1, selectcmpInst->getPredicate()) ) { continue; }

          icomps.push_back(selectcmpInst);
        }
      }
    }
  }
 
  for (auto &IcmpInst: icomps) {

    bool flagged = isFlagged(*IcmpInst);
    BasicBlock* bb = IcmpInst->getParent();
    bool presentInDict = false;

    auto op0 = IcmpInst->getOperand(0);
    auto op1 = IcmpInst->getOperand(1);
    auto pred = dyn_cast<CmpInst>(IcmpInst)->getPredicate();

    /* Add current instruction to our dictionary */
    if ( !flagged && (pred == CmpInst::ICMP_EQ || pred == CmpInst::ICMP_NE)) {
      std::string debugInfo = utils::getDebugInfo(*IcmpInst);      
      presentInDict = recordToDictionary(*IcmpInst, dictAdded, *op0, *op1, pred, debugInfo);
      /* if we added to dictionary and user requested not to convert to byte comparison, bail out */
      if ( presentInDict && !utils::isEnvVarSetTo("AFL_CONVERT_COMPARISON_TYPE", "ALL") ) {
        continue;
      }
    }

    if ( utils::isEnvVarSetTo("AFL_CONVERT_COMPARISON_TYPE", "NONE") ) { continue; }

    /* Cannot half if it's only 8 bytes */
    if ( !(bitw > 8) ) { continue; }

    /* Record number of *original* instructions instrumented */
    Processed += (flagged == false);

    BasicBlock* end_bb = bb->splitBasicBlock(BasicBlock::iterator(IcmpInst)); ASSERT (end_bb);
    end_bb->setName(StartName + Twine("NewBB.ICmp.High.False"));

    /* create the comparison of the top halfs of the original operands */
    Instruction *s_op0, *op0_high, *s_op1, *op1_high, *icmp_high;

    s_op0 = BinaryOperator::Create(Instruction::LShr, op0, ConstantInt::get(OldIntType, bitw / 2), StartName + Twine("Upper0"));
    bb->getInstList().insert(bb->getTerminator()->getIterator(), s_op0);
    op0_high = new TruncInst(s_op0, NewIntType, StartName + Twine("TruncUpper0"));
    bb->getInstList().insert(bb->getTerminator()->getIterator(), op0_high);

    s_op1 = BinaryOperator::Create(Instruction::LShr, op1, ConstantInt::get(OldIntType, bitw / 2), StartName + Twine("Upper1"));
    bb->getInstList().insert(bb->getTerminator()->getIterator(), s_op1);
    op1_high = new TruncInst(s_op1, NewIntType, StartName + Twine("TruncUpper1"));
    bb->getInstList().insert(bb->getTerminator()->getIterator(), op1_high);

    icmp_high = CmpInst::Create(Instruction::ICmp, pred, op0_high, op1_high, StartName + Twine("ICmp.High"));
    bb->getInstList().insert(bb->getTerminator()->getIterator(), icmp_high);

    /* now we have to destinguish between == != and > < */
    if (pred == CmpInst::ICMP_EQ || pred == CmpInst::ICMP_NE) {
      
      /* transformation for == and != icmps */

      /* create a compare for the lower half of the original operands */
      Instruction *op0_low, *op1_low, *icmp_low;
      BasicBlock* cmp_low_bb = BasicBlock::Create(C, StartName + Twine("NewBB.ICmp.Low"), end_bb->getParent(), end_bb);

      op0_low = new TruncInst(op0, NewIntType, StartName + Twine("Lower0"));
      cmp_low_bb->getInstList().push_back(op0_low);

      op1_low = new TruncInst(op1, NewIntType, StartName + Twine("Lower1"));
      cmp_low_bb->getInstList().push_back(op1_low);

      icmp_low = CmpInst::Create(Instruction::ICmp, pred, op0_low, op1_low,  StartName + Twine("ICmp.Low"));
      cmp_low_bb->getInstList().push_back(icmp_low);
      BranchInst::Create(end_bb, cmp_low_bb);

      /* dependant on the cmp of the high parts go to the end or go on with
       * the comparison */
      auto term = bb->getTerminator();
      if (pred == CmpInst::ICMP_EQ) {
        BranchInst::Create(cmp_low_bb, end_bb, icmp_high, bb);
      } else {
        /* CmpInst::ICMP_NE */
        BranchInst::Create(end_bb, cmp_low_bb, icmp_high, bb);
      }
      term->eraseFromParent();

      /* create the PHI and connect the edges accordingly */
      PHINode *PN = PHINode::Create(Int1Ty, 2, "");
      PN->addIncoming(icmp_low, cmp_low_bb);
      if (pred == CmpInst::ICMP_EQ) {
        PN->addIncoming(ConstantInt::get(Int1Ty, 0), bb);
      } else {
        /* CmpInst::ICMP_NE */
        PN->addIncoming(ConstantInt::get(Int1Ty, 1), bb);
      }

      
      // SmallVector<StringRef, 8> Names;
      // M.getMDKindNames(Names);
      // for (auto n : Names) {
      //   errs() << n << ":" << C.getMDKindID(n) << "\n";
      // }
      // SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
      // icmp_high->getAllMetadata(MDs);
      // for (auto &MD : MDs) {
      //   errs() << "MD:" << MD.first << "," << *MD.second << "\n";
      // }
      // MDs.clear();

      /* Add the newly-create top instruction to the dictionary, if it's present, so as to keep the top-most instruction 
         Note: we don't need to add to icmp_low because any seed visiting icmp_high will visit icmp_high
      */
      if ( utils::isDictRecordedToInstr(*IcmpInst, C2U_DICT) ) {
        ASSERT ( utils::CopyDictToInst(C, C2U_DICT, *IcmpInst, *icmp_high) );
      }

      /* Flag the two newly-created instructions so we don't try to add them to dictionary later */
      flagInstruction(*icmp_high);
      flagInstruction(*icmp_low);

      /* replace the old icmp with the new PHI */
      BasicBlock::iterator ii(IcmpInst);
      ReplaceInstWithInst(IcmpInst->getParent()->getInstList(), ii, PN);   


    } else {
      /* CmpInst::ICMP_UGT and CmpInst::ICMP_ULT */
      /* transformations for < and > */

      /* create a basic block which checks for the inverse predicate. 
       * if this is true we can go to the end if not we have to got to the
       * bb which checks the lower half of the operands */
      Instruction *icmp_inv_cmp, *op0_low, *op1_low, *icmp_low;
      BasicBlock* inv_cmp_bb = BasicBlock::Create(C,  StartName + Twine("NewBB.Inv_cmp"), end_bb->getParent(), end_bb);
      if (pred == CmpInst::ICMP_UGT) {
        icmp_inv_cmp = CmpInst::Create(Instruction::ICmp, CmpInst::ICMP_ULT, op0_high, op1_high, StartName + Twine("ICmp.ULT"));
      } else {
        icmp_inv_cmp = CmpInst::Create(Instruction::ICmp, CmpInst::ICMP_UGT, op0_high, op1_high, StartName + Twine("ICMP.UGT"));
      }
      inv_cmp_bb->getInstList().push_back(icmp_inv_cmp);

      auto term = bb->getTerminator();
      term->eraseFromParent();
      BranchInst::Create(end_bb, inv_cmp_bb, icmp_high, bb);

      /* create a bb which handles the cmp of the lower halfs */
      BasicBlock* cmp_low_bb = BasicBlock::Create(C, StartName + Twine("LowerBB.Low"), end_bb->getParent(), end_bb);
      op0_low = new TruncInst(op0, NewIntType, StartName + Twine("Lower0"));
      cmp_low_bb->getInstList().push_back(op0_low);
      op1_low = new TruncInst(op1, NewIntType, StartName + Twine("Lower1"));
      cmp_low_bb->getInstList().push_back(op1_low);

      icmp_low = CmpInst::Create(Instruction::ICmp, pred, op0_low, op1_low, StartName + Twine("ICmp.Low"));
      cmp_low_bb->getInstList().push_back(icmp_low);
      BranchInst::Create(end_bb, cmp_low_bb);

      BranchInst::Create(end_bb, cmp_low_bb, icmp_inv_cmp, inv_cmp_bb);

      PHINode *PN = PHINode::Create(Int1Ty, 3);
      PN->addIncoming(icmp_low, cmp_low_bb);
      PN->addIncoming(ConstantInt::get(Int1Ty, 1), bb);
      PN->addIncoming(ConstantInt::get(Int1Ty, 0), inv_cmp_bb);

      /* Note: we don't copy over the dictionary information because dictionary is not recorded for non-equalitiy predicates */
      BasicBlock::iterator ii(IcmpInst);
      ReplaceInstWithInst(IcmpInst->getParent()->getInstList(), ii, PN);
    }
  }

  return Processed;
}

bool Compare2Unit::runOnModule(Module &M) {
  
  /* Show a banner */

  char be_quiet = 0;

  /* Make sure AFL_CONVERT_COMPARISON_TYPE is set */
  if ( !utils::isEnvVarSet("AFL_CONVERT_COMPARISON_TYPE") ) {
    FATAL("AFL_CONVERT_COMPARISON_TYPE not set. Option={ALL,NONE,NOT_DICT}");
  }

  bool convertOK = utils::isEnvVarSetTo("AFL_CONVERT_COMPARISON_TYPE", "ALL") || 
                   utils::isEnvVarSetTo("AFL_CONVERT_COMPARISON_TYPE", "NONE")||
                   utils::isEnvVarSetTo("AFL_CONVERT_COMPARISON_TYPE", "NOT_DICT");
  if (!convertOK) {
    FATAL("Invalid AFL_CONVERT_COMPARISON_TYPE. Option={ALL,NONE,NOT_DICT}");
  }

  if (isatty(2) && !getenv("AFL_QUIET")) {

    SAYF(cCYA "compare-to-unit pass " cBRI VERSION cRST "\n");

    OKF("AFL_CONVERT_COMPARISON_TYPE = %s", utils::getEnvVar("AFL_CONVERT_COMPARISON_TYPE"));

  } else be_quiet = 1;


  size_t dictAdded = 0;
  std::set<CmpInst*> ListCmp;
  size_t Updates = 0;

  if ( !utils::isEnvVarSetTo("AFL_CONVERT_COMPARISON_TYPE", "NONE") ) {
    Updates += simplifyCompares(M);
    Updates += simplifySignedness(M);
  }

  //recordComparesToInstrument(M, ListCmp, Dictionary);
  Updates += halfComparesAndRecord(M, 64, dictAdded);
  Updates += halfComparesAndRecord(M, 32, dictAdded);
  Updates += halfComparesAndRecord(M, 16, dictAdded);
  Updates += halfComparesAndRecord(M, 8, dictAdded);

  //verifyModule(M);

  /* Update the file containing dictionary */
  #if 0
  if ( dictAdded ) {

    char* dict_file = getenv("AFL_BCCLANG_DICT_FILE");
    if (!dict_file) {
      FATAL("AFL_BCCLANG_DICT_FILE not defined");
    }

    std::fstream outfs;
    outfs.open(dict_file, std::fstream::out|std::fstream::app);
    ASSERT ( outfs.is_open() );
    u32 n = 1;
    for (auto & elt : Dictionary) {
      std::string ss = utils::Stringify(elt.getFirst(), false);
      std::string debug = elt.getSecond();
      if (ss.size()) {
        outfs << "C2U_" << std::setw(8) << std::setfill('0') << std::hex << n++ << "_" << (debug.size()?debug:"NDEBUG") << "=\"" << ss << "\"\n";
      }
    }
  }
  #endif

  if (!be_quiet) {

    if (!Updates) WARNF("No instrumentation ICMP found.");
    else OKF("Instrumented %zu ICMP.", Updates);

    if (!dictAdded) WARNF("No entries added to DICT.");
    else OKF("Added %zu entries to DICT.", dictAdded);

  }

  return true;
}

static void registerSplitComparesPass(const PassManagerBuilder &,
                         legacy::PassManagerBase &PM) {
  PM.add(new Compare2Unit());
}

static RegisterStandardPasses RegisterSplitComparesPass(
    PassManagerBuilder::EP_OptimizerLast, registerSplitComparesPass);

static RegisterStandardPasses RegisterSplitComparesTransPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0, registerSplitComparesPass);

