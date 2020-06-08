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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fstream>
#include <set>

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
#include "llvm/IR/LLVMContext.h"
#include "llvm/Analysis/ValueTracking.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DebugInfo.h"
#include "afl-llvm-pass-parent.h"

#define S2U_BB_NAME "S2U."

using namespace llvm;

#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
static LLVMContext TheContext;
#endif

namespace {

  class StrCompare2Unit : public AFLPassParent, public ModulePass {

    public:
      static char ID;
      StrCompare2Unit() : ModulePass(ID) {
      } 

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
        return "splits comparison functions";
      }

    private:
      utils::DictElt getFromDictAndRemoveOriginal(CallInst & CI, bool & found);
      bool isRecordedInDictionary(CallInst & CI);
      void addToDictionary(CallInst & CI, utils::DictElt elmt, size_t & dictAdded);
      void visitStructure(Constant * C, const DataLayout & DL, CallInst & CI, size_t & dictAdded);
      void visitInteger(const StructLayout * SL, Type * T, unsigned idx, Constant * CstInit, CallInst & CI, size_t & dictAdded);
      void harvestConstantArrays(Module &M, size_t & dictAdded);
      void harvestConstantStores(Module &M, size_t & dictAdded);
      void harvestConstantStructs(Module &M, size_t & dictAdded);
      bool transformCmps(Module &M, unsigned & processedStrcmp, unsigned & processedStrncmp, 
                      unsigned & processedMemcmp, unsigned & processedBothVariable, 
                      size_t & dictAdded);
  };
}


char StrCompare2Unit::ID = 0;

bool StrCompare2Unit::transformCmps(Module &M, unsigned & processedStrcmp, 
                                  unsigned & processedStrncmp, unsigned & processedMemcmp,
                                  unsigned & processedBothVariable, size_t & dictAdded) {

  typedef struct {
    CallInst* inst;
    enum {STRCMP=0, STRNCMP, MEMCMP} type;
    bool bothVariable;
  } callInfo_t;

  std::vector<callInfo_t> calls;
  LLVMContext &C = M.getContext();
  IntegerType *Int8Ty = IntegerType::getInt8Ty(C);
  IntegerType *Int32Ty = IntegerType::getInt32Ty(C);
  IntegerType *Int64Ty = IntegerType::getInt64Ty(C);

// #if LLVM_VERSION_CODE < LLVM_VERSION(6, 0)
//   WARNF("Array splitting not supported yet for LLVM version %d.%d", LLVM_VERSION_MAJOR, LLVM_VERSION_MINOR);
// #endif
  
  /* iterate over all functions, bbs and instruction and add suitable calls to strcmp/memcmp */
  for (auto &F : M) {
    for (auto &BB : F) {
      for(auto &IN: BB) {
        CallInst* callInst = nullptr;
        
        if ((callInst = dyn_cast<CallInst>(&IN))) {

          bool bothVariable = false;
          bool isStrcmp = false;
          bool isStrncmp = false;
          bool isMemcmp = false;

          Function *Callee = callInst->getCalledFunction();
          if (!Callee)
            continue;
          if (callInst->getCallingConv() != llvm::CallingConv::C)
            continue;
          StringRef FuncName = Callee->getName();

          isStrcmp |= !FuncName.compare(StringRef("strcmp")) || !FuncName.compare(StringRef("strcasecmp"));
          isStrncmp |= !FuncName.compare(StringRef("strncmp")) || !FuncName.compare(StringRef("strncasecmp"));
          isMemcmp |= !FuncName.compare(StringRef("memcmp"));

          /* Verify the strcmp/memcmp function prototype */
          FunctionType *FT = Callee->getFunctionType();
          isStrcmp &= FT->getNumParams() == 2 &&
                      FT->getReturnType()->isIntegerTy(32) && 
                      FT->getParamType(0) == FT->getParamType(1) && 
                      FT->getParamType(0) == IntegerType::getInt8PtrTy(M.getContext());
          isStrncmp &= FT->getNumParams() == 3 && 
                      FT->getParamType(0)->isPointerTy() &&
                      FT->getParamType(1)->isPointerTy() &&
                      FT->getReturnType()->isIntegerTy(32);
          isMemcmp &= FT->getNumParams() == 3 && 
                      FT->getParamType(0)->isPointerTy() &&
                      FT->getParamType(1)->isPointerTy() &&
                      FT->getReturnType()->isIntegerTy(32);


          if (!isStrcmp && !isStrncmp && !isMemcmp)
            continue;

          
          /* is a strcmp/memcmp, check is we have strcmp(x, "const") or strcmp("const", x)
             memcmp(x, "const", ..) or memcmp("const", x, ..) 
             We must also consider the case of memcmp/strcmp with an array rather than string
          */
          Value *Str1P = callInst->getArgOperand(0), *Str2P = callInst->getArgOperand(1);
          StringRef Str1, Str2;
          bool HasStr1 = getConstantStringInfo(Str1P, Str1, 0, false);
          bool HasStr2 = getConstantStringInfo(Str2P, Str2, 0, false);
#if 0
#if LLVM_VERSION_CODE >= LLVM_VERSION(6, 0)
          bool HasArr1 = isa<ConstantDataArray>(Str1P);
          bool HasArr2 = isa<ConstantDataArray>(Str2P);

          errs() << "test array\n";
          ConstantDataArraySlice Slice1;
          errs() << "1:" << *Str1P << "\n";
          errs() << "2:" << *Str2P << "\n";
          if (getConstantDataArrayInfo(Str1P, Slice1, Int8Ty->getBitWidth()/8, 0)) {
            errs() << "got data 1\n";
          }

          ConstantDataArraySlice Slice2;
          if (getConstantDataArrayInfo(Str2P, Slice2, Int8Ty->getBitWidth()/8, 0)) {
            errs() << "got data 2\n";
          }
#endif
          // if ( HasArr1 ) {
          //   errs() << "arra1\n";
          // }

          // if ( HasArr2 ) {
          //   errs() << "arra2\n";
          // }
#endif

          /* Handle the case when the two params are variables, ie not constant */
          bothVariable = !HasStr1 && !HasStr2;
          if ( bothVariable && isStrcmp) {
            continue; /* We cannot handle this */
          }

          if (!(HasStr1 ^ HasStr2) && !bothVariable) {
            std::string Info = utils::getDebugInfo(*callInst);
            WARNF("Found comparison between two constant strings (%s)!?", Info.c_str());
            continue;
          }

          if (isMemcmp || isStrncmp) {
            /* check if third operand is a constant integer
             * strlen("constStr") and sizeof() are treated as constant */
            Value *op2 = callInst->getArgOperand(2);
            ConstantInt* ilen = dyn_cast<ConstantInt>(op2);
            if (!ilen)
              continue;

            /* final precaution: if size of compare is larger than constant string:
              - it's a bug for memcmp! 
              - it's OK for strcmp and strncmp as tailing '\0' will not match
            */
            if ( !bothVariable ) {
              uint64_t literalLengh = HasStr1 ? Str1.size() : Str2.size();
              if (literalLengh < ilen->getZExtValue() && isMemcmp) {
                BADF("Found a memcmp() bug");
              }
            }
          }

          calls.push_back({callInst, 
                            isStrcmp ? callInfo_t::STRCMP : isStrncmp ? callInfo_t::STRNCMP : callInfo_t::MEMCMP, 
                            bothVariable});
        }
      }
    }
  }

  if (!calls.size())
    return false;
  
  for (callInfo_t Info: calls) {

    CallInst * callInst = Info.inst; ASSERT (callInst);
    int type = Info.type;
    bool bothVariable = Info.bothVariable;

    Value *Str1P = callInst->getArgOperand(0), *Str2P = callInst->getArgOperand(1);
    StringRef Str1, Str2, ConstStr;
    Value *VarStr;
    bool HasStr1 = getConstantStringInfo(Str1P, Str1, 0, false);
    bool HasStr2 = getConstantStringInfo(Str2P, Str2, 0, false);
    uint64_t constLen, lenArg;
    bool isMemcmp = (type==callInfo_t::MEMCMP);
    bool isStrcmp = (type==callInfo_t::STRCMP);
    bool isStrncmp = (type==callInfo_t::STRNCMP);
    bool useLengthArg = isMemcmp || isStrncmp || bothVariable;

    if (useLengthArg) {
      Value *op2 = callInst->getArgOperand(2);
      ConstantInt* ilen = dyn_cast<ConstantInt>(op2);
      lenArg = ilen->getZExtValue();
    }

    /* Note: for strncmp(s, "thestring", thelen), we could optimize further by taking the min(len("thestring"), thelen) */
    if (HasStr1) {
      ConstStr = Str1;
      VarStr = Str2P;
      constLen = useLengthArg ? lenArg : Str1.size();
    } else if (HasStr2) {
      ConstStr = Str2;
      VarStr = Str1P;
      constLen = useLengthArg ? lenArg : Str2.size();
    } else {
      ASSERT (bothVariable && useLengthArg);
      constLen = lenArg;
    }

    /* This happens with empty string "" */
    if ( constLen == 0 ) { continue; }

    /* Add the string to our dictionary */
    if (bothVariable) {
      //errs() << "both variable\n";
      //errs() << "bothVariable:" << *callInst << "\n";
      //ASSERT ( !isRecordedInDictionary(*callInst) );
    } else {
      
      std::string KeyWord(ConstStr.data(), constLen);
      std::string debugInfo = utils::getDebugInfo(*callInst);
      utils::DictElt elmt = utils::DictElt(KeyWord, debugInfo);
      ASSERT ( !isRecordedInDictionary(*callInst) );
      addToDictionary(*callInst, elmt, dictAdded);
      
      /* if we added to dictionary and user requested not to convert to byte comparison, bail out */
      if ( !utils::isEnvVarSetTo("AFL_CONVERT_COMPARISON_TYPE", "ALL") ) {
        continue;
      }
    }

    if ( utils::isEnvVarSetTo("AFL_CONVERT_COMPARISON_TYPE", "NONE") ) { continue; }

    /* Check if the instruction already has a dictionary attached to it 
       Note: this is possible because of the earlier call to harvestConstantStores()
       so it would be bothVariable yet recorded
    */
    if ( utils::isEnvVarSetTo("AFL_CONVERT_COMPARISON_TYPE", "NOT_DICT") && 
         isRecordedInDictionary(*callInst) ) { continue; }

    /* Increment our numbers */
    processedStrcmp += isStrcmp;
    processedStrncmp += isStrncmp;
    processedMemcmp += isMemcmp;
    processedBothVariable += (bothVariable == true);


    /* Retrieve the dictionary word before changing the IR */
    bool foundDict = false;
    //errs() << *callInst << "\n";
    utils::DictElt elmt = getFromDictAndRemoveOriginal(*callInst, foundDict);
    if (!bothVariable) {
      //elemt = utils::getDictRecordFromInstr(C, *callInst, S2U_DICT, foundDict);
      ASSERT ( foundDict );   
    } else {
      //elemt = utils::getDictRecordFromInstr(C, *callInst, S2U_DICT, foundDict);
      //errs() << foundDict << " for " << *callInst << "\n";
    }

    /* split before the call instruction */
    BasicBlock *bb = callInst->getParent();
    BasicBlock *end_bb = bb->splitBasicBlock(BasicBlock::iterator(callInst));
    BasicBlock *next_bb =  BasicBlock::Create(C, Twine(S2U_BB_NAME) + Twine("New.Cmp"), end_bb->getParent(), end_bb);
    BranchInst::Create(end_bb, next_bb);
    PHINode *PN = PHINode::Create(Int32Ty, constLen, Twine(S2U_BB_NAME) + Twine("Cmp_phi"));

    TerminatorInst *term = bb->getTerminator();
    BranchInst::Create(next_bb, bb);
    term->eraseFromParent();

    for (uint64_t i = 0; i < constLen; i++) {

      BasicBlock *cur_bb = next_bb;

      BasicBlock::iterator IP = next_bb->getFirstInsertionPt();
      IRBuilder<> IRB(&*IP);

      Value* v = ConstantInt::get(Int64Ty, i);
      Value *isub;

      if (bothVariable) {

        Value *ele  = IRB.CreateInBoundsGEP(Str1P, v, Twine(S2U_BB_NAME) + Twine("GEP.Var1"));
        Value *load = IRB.CreateLoad(ele);

        Value *ele2  = IRB.CreateInBoundsGEP(Str2P, v, Twine(S2U_BB_NAME) + Twine("GEP.Var2"));
        Value *load2 = IRB.CreateLoad(ele2);
        isub = IRB.CreateSub(load, load2);

      } else {

        char c = ConstStr[i];
        Value *ele  = IRB.CreateInBoundsGEP(VarStr, v, Twine(S2U_BB_NAME) + Twine("GEP"));
        Value *load = IRB.CreateLoad(ele);
        isub = IRB.CreateSub(ConstantInt::get(Int8Ty, c), load);
        
      }

      Value *sext = IRB.CreateSExt(isub, Int32Ty); 
      PN->addIncoming(sext, cur_bb);

      if (i < constLen - 1) {
        next_bb =  BasicBlock::Create(C, Twine(S2U_BB_NAME) + Twine("New.Cmp"), end_bb->getParent(), end_bb);
        BranchInst::Create(end_bb, next_bb);

        TerminatorInst *term = cur_bb->getTerminator();
        Value *icmp = IRB.CreateICmpEQ(isub, ConstantInt::get(Int8Ty, 0));
        BranchInst * BI = IRB.CreateCondBr(icmp, next_bb, end_bb);
        term->eraseFromParent();

        /* Add the dictionary for the new instruction */
        if (i==0 && foundDict) {
          utils::recordDictToInstr(C, *BI, elmt, S2U_DICT, false);
        }

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

void StrCompare2Unit::harvestConstantStores(Module &M, size_t & dictAdded) {
  /*
    This is similar to harvestConstantArrays()
    The main difference is we are looking for store instruction instead of a call to memcpy()
    The reason is that the instcombine pass does transform memcpy/memset/memmov into a store
    if the size of the argument is 1,2,4, or 8
    Unfortunately I cannot remove the instcombine pass because I need it to remove integer promotion...
  */

  for (auto &F : M) {
    for (auto &BB : F) {
      for(auto &IN: BB) {
        
        StoreInst * SI = nullptr;
        ConstantInt * CI = nullptr;
        if (!(SI = dyn_cast<StoreInst>(&IN))) { continue; }

        Value * V = SI->getValueOperand();
        if ( !(CI = dyn_cast<ConstantInt>(V)) ) { continue; }

        //errs() <<  *CI << "\n";

        std::vector<llvm::Instruction*> Userlist;
        utils::getUsersOf(*SI->getOperand(1), Userlist);
        //errs() << Userlist.size() << "\n";
        for ( size_t ii=0; ii<Userlist.size(); ++ii ) {
          //errs() << *Userlist[ii] << "\n";
          if (Userlist[ii] != SI) {
            std::vector<llvm::Instruction*> SecUserlist;
            utils::getUsersOf(*Userlist[ii], SecUserlist);
            for ( size_t iii=0; iii<SecUserlist.size(); ++iii ) {
              //errs() << "iii:" << *SecUserlist[iii] << "\n";
              CallInst * callInst = nullptr;
              if ( (callInst = dyn_cast<CallInst>(SecUserlist[iii])) ) {
                Function * f = callInst->getCalledFunction();
                if (f) {
                  bool found = false, Mempcmp = false;
                  found |= !f->getName().compare(StringRef("strcmp")) || !f->getName().compare(StringRef("strcasecmp"));
                  found |= !f->getName().compare(StringRef("strncmp")) || !f->getName().compare(StringRef("strncasecmp"));
                  Mempcmp |= !f->getName().compare(StringRef("memcmp"));
                  found |= Mempcmp;
                  // TODO: check function type
                  // TODO convention call
                  if (found) {
                    
                    ASSERT(sys::IsLittleEndianHost);
                    uint64_t Val = CI->getZExtValue();
                    size_t nBytes = CI->getBitWidth() / 8;
                    char buf[8]; /* Max size */
                    memset(buf, 0, sizeof(buf));
                    memcpy(buf, (u8*)&Val, nBytes);

                    std::string KeyWord(buf, nBytes);
                    std::string debugInfo = utils::getDebugInfo(*callInst);
                    utils::DictElt elmt = utils::DictElt(KeyWord, debugInfo);
                    //errs() << "found:" << Val << "\n";
                    // if ( !dict.count(elmt) ) {
                    //   dict.insert(elmt);
                    // }
                    // size_t & dictAdded
                    // TODO: approximation: don't use the

                    /* get the user of the call instruction, and mark this instruction */
                    addToDictionary(*callInst, elmt, dictAdded);
                    //errs() << "added1:" << *callInst << "\n";
                  }
                }
              }
            }
          }
        }
      }
    }
  }

}

bool StrCompare2Unit::isRecordedInDictionary(CallInst & CI) {
  std::vector<llvm::Instruction*> CallUserlist;
  utils::getUsersOf(CI, CallUserlist);
  for ( size_t u=0; u<CallUserlist.size(); ++u ) {
    //errs() << "u:" << *CallUserlist[u] << "\n";
    CmpInst * CmpI = nullptr;
    PHINode * Phi = nullptr;
    if ( ( CmpI = dyn_cast<CmpInst>(CallUserlist[u])) ) {
      if ( utils::isDictRecordedToInstr(*CmpI, S2U_DICT) ) {
        return true;
      }
    } else if ( ( Phi = dyn_cast<PHINode>(CallUserlist[u])) ) {
      if ( utils::isDictRecordedToInstr(*Phi, S2U_DICT) ) {
        return true;
      }
    }
  }
  return false;
}

void StrCompare2Unit::addToDictionary(CallInst & CI, utils::DictElt elmt, size_t & dictAdded) {
  std::vector<llvm::Instruction*> CallUserlist;
  size_t n = 0;
  utils::getUsersOf(CI, CallUserlist);
  for ( size_t u=0; u<CallUserlist.size(); ++u ) {
    //errs() << "u:" << *CallUserlist[u] << "\n";
    CmpInst * CmpI = nullptr;
    PHINode * Phi = nullptr;
    if ( ( CmpI = dyn_cast<CmpInst>(CallUserlist[u])) ) {
      ASSERT ( !utils::isDictRecordedToInstr(*CmpI, S2U_DICT) ); 
      utils::recordDictToInstr(getGlobalContext(), *CmpI, elmt, S2U_DICT, true, true);
      ++n;
    } else if ( ( Phi = dyn_cast<PHINode>(CallUserlist[u])) ) {
      ASSERT ( !utils::isDictRecordedToInstr(*Phi, S2U_DICT) ); 
      utils::recordDictToInstr(getGlobalContext(), *Phi, elmt, S2U_DICT, true, true);
      ++n;
    }
  }

  //ASSERT ( n<=1 ); too conservative?
  dictAdded += n;
}

utils::DictElt StrCompare2Unit::getFromDictAndRemoveOriginal(CallInst & CI, bool & found) {
  std::vector<llvm::Instruction*> CallUserlist;
  utils::getUsersOf(CI, CallUserlist);
  std::vector<utils::DictElt> list;
  for ( size_t u=0; u<CallUserlist.size(); ++u ) {
    //errs() << "u:" << *CallUserlist[u] << "\n";
    CmpInst * CmpI = nullptr;
    PHINode * Phi = nullptr;
    if ( ( CmpI = dyn_cast<CmpInst>(CallUserlist[u])) ) {
      bool found = false;
      utils::DictElt elmt = utils::getDictRecordFromInstr(getGlobalContext(), *CmpI, S2U_DICT, found, true /* remove original meta */);
      if ( found ) {
        list.push_back( elmt );
      }
    } else if ( ( Phi = dyn_cast<PHINode>(CallUserlist[u])) ) {
      bool found = false;
      utils::DictElt elmt = utils::getDictRecordFromInstr(getGlobalContext(), *Phi, S2U_DICT, found, true /* remove original meta */);
      if ( found ) {
        list.push_back( elmt );
      }
    }
  }

  //ASSERT ( list.size() <= 1 ); /* 0 or exactly 1 entry */
  found = !!list.size();
  return found ? list[0] : utils::DictElt("","");
}

void StrCompare2Unit::visitInteger(const StructLayout * SL, Type * T, unsigned idx, Constant * CstInit, CallInst & CI, size_t & dictAdded) {
  ASSERT(T->isIntegerTy());
  uint64_t EltOffset = SL->getElementOffset(idx);
  unsigned Op = SL->getElementContainingOffset(EltOffset);
  // Type * t = CstInit->getOperand(Op)->getType(); ASSERT(t);
  // errs() << "isa1:" << isa<ConstantInt>(CstInit->getOperand(Op)) << "\n";
  // errs() << "isa2:" << isa<Constant>(CstInit->getOperand(Op)) << "\n";
  // errs() << "t:" << *CstInit->getOperand(Op)->getType() << "\n";
  // errs() << "C:" << *cast<Constant>(CstInit->getOperand(Op)) << "\n";
  // errs() << "s=" << s << "\n";
  if (!isa<ConstantInt>(CstInit->getOperand(Op))) { return; }
  ConstantInt * CInt = cast<ConstantInt>(CstInit->getOperand(Op));
  uint64_t Val = CInt->getZExtValue();
  unsigned nBytes = CInt->getBitWidth() / 8;
  ASSERT( T->getPrimitiveSizeInBits() / 8 == nBytes && "Invalid number of bytes");
  ASSERT( nBytes <= 8 );
  
  char buf[8]; /* Max size */
  memset(buf, 0, sizeof(buf));
  ASSERT(sys::IsLittleEndianHost);
  #if 0
  if(sys::IsBigEndianHost) {
    u8 * p = (u8*)&Val;
    for ( size_t i=0; i<8/2; ++i ) {
      uint8_t tmp = p[i];
      p[i] = p[8 - i - 1];
      p[8 - i - 1] = tmp;
    }
  }
  #endif
  memcpy(buf, (u8*)&Val, nBytes);
  //printf("adding:%8lX\n", Val);
  std::string KeyWord(buf, nBytes);
  std::string debugInfo = utils::getDebugInfo(CI);
  //errs() << "debug:" << debugInfo << "\n";
  utils::DictElt elmt = utils::DictElt(KeyWord, debugInfo);
  utils::recordMultipleDictToInstr(getGlobalContext(), CI, elmt, S2U_DICT, true, false);
  //errs() << *CI << "\n";
  ++dictAdded;
}

void StrCompare2Unit::visitStructure(Constant * CstInit, const DataLayout & DL, CallInst & CI, size_t & dictAdded) {
  // errs() << "struct:" << *CstInit->getType() << "\n";
  // errs() << "GV=:" << *GV << "\n";
  // errs() << "CstInit:" << *CstInit << "\n";
  
  /* Add src to dictionary */
  StructType * ST = cast<StructType>(CstInit->getType()); ASSERT(ST);
  ConstantStruct * CS = dyn_cast<ConstantStruct>(CstInit); 
  if (!CS) { return; /* May be initialized to null */ }

  /* see http://llvm.org/doxygen/WholeProgramDevirt_8cpp_source.html */
  const StructLayout * SL = DL.getStructLayout(CS->getType()); ASSERT(SL);

  for ( size_t i=0; i<ST->getNumElements(); ++i ) {
    Type * T = ST->getElementType(i); ASSERT(T);
    // TODO: support uninon,etc
    if ( T->isStructTy() ) {
      uint64_t EltOffset = SL->getElementOffset(i);
      unsigned Op = SL->getElementContainingOffset(EltOffset);
      ConstantStruct * CS = dyn_cast<ConstantStruct>(CstInit->getOperand(Op)); 
      if (!CS) { continue; /* May be initialized to null */ }
      visitStructure(CS, DL, CI, dictAdded);
    } else if ( T->isIntegerTy() ) {
      //errs() << "c:" << *T << "\n";
      visitInteger(SL, T, i, CstInit, CI, dictAdded);
    }
  }
}


void StrCompare2Unit::harvestConstantStructs(Module &M, size_t & dictAdded) {

  /*
    Search for calls that take as input a constant struct, ie a structure which is filled with constant values
    We only check for constant integer for now. TODO: support more types    
  */
  const DataLayout &DL = M.getDataLayout();
  for (auto &F : M) {
    for (auto &BB : F) {
      for(auto &IN: BB) {

        CallInst* CI = nullptr;
        
        if (!(CI = dyn_cast<CallInst>(&IN))) { continue; }

        if (CI->getCallingConv() != llvm::CallingConv::C) { continue; }

        Function * fun = CI->getCalledFunction();
        if (!fun) { continue; }

        FunctionType *FT = fun->getFunctionType();
        //errs() << "fun:" << fun->getName() << "\n";

        for ( size_t i=0; i<FT->getNumParams(); ++i ) {
          
          /* Get second parameter and check if we have it in our dictionary */
          auto src = CI->getOperand(i)->stripPointerCasts();
          //errs() << "src:" << *src << "\n";
          GlobalVariable * GV = nullptr;
          if ( !(GV = dyn_cast<GlobalVariable>(src))) { continue; }
          
          if ( !GV->isConstant()) { continue; }

          if ( !GV->hasInitializer() ) { 
            ASSERT( GV->isDeclaration() ); 
            continue; 
          }        
          
          /* Get the initializer data */
          Constant * CstInit = GV->getInitializer(); ASSERT (CstInit);
          if ( CstInit->getType()->getTypeID() != Type::StructTyID ) { continue; }

          visitStructure(CstInit, DL, *CI, dictAdded);
        }
      }
    }
  }
}

void StrCompare2Unit::harvestConstantArrays(Module &M, size_t & dictAdded) {

  /*
    Explanation: a crude way to find the array
    1) first look for memcpy(dst, src, len) where src is a global constant array
    2) check the users of dst, and look for a call to a function of interest
    Note that if we skip 2), we will find the array but there may be false positives, ie arrays
    not used into a function of interest
  */
  for (auto &F : M) {
    for (auto &BB : F) {
      for(auto &IN: BB) {

        CallInst* CI = nullptr;
        
        if (!(CI = dyn_cast<CallInst>(&IN))) { continue; }

        if (CI->getCallingConv() != llvm::CallingConv::C) { continue; }

        Function * fun = CI->getCalledFunction();
        
        if ( !(fun && fun->getName().startswith("llvm.memcpy") ) ) { continue; }
            
        /* Get second parameter and check if we have it in our dictionary */
        auto src = CI->getOperand(1)->stripPointerCasts();
        
        GlobalVariable * GV = nullptr;
        if ( !(GV = dyn_cast<GlobalVariable>(src))) { continue; }
        
        if ( !GV->isConstant()) { continue; }             

        /* Get the initializer data */
        Constant * CstInit = GV->getInitializer(); ASSERT (CstInit);
       
        if ( CstInit->getType()->getTypeID() != Type::ArrayTyID) { 
          /* We're catching structure in harvestConstantStructs(). Are there others
          we should deal with? */
          ASSERT( CstInit->getType()->getTypeID() == Type::StructTyID ); 
          continue; 
        }

        //errs() << "str:" << ConstStr << "\n";
        std::vector<llvm::Instruction*> Userlist;
        utils::getUsersOf(*CI->getOperand(0)->stripPointerCasts(), Userlist);
        //errs() << *CI->getOperand(0)->stripPointerCasts() << "\n";
        //errs() << "size:" << Userlist.size() << "\n";
        for ( size_t ii=0; ii<Userlist.size(); ++ii ) {
          //errs() << "use:" << *Userlist[ii] << "\n";
          if (Userlist[ii] != CI) {
            std::vector<llvm::Instruction*> SecUserlist;
            utils::getUsersOf(*Userlist[ii], SecUserlist);
            for ( size_t iii=0; iii<SecUserlist.size(); ++iii ) {
              //errs() << "user2:" << *SecUserlist[iii] << "\n";

              CallInst * callInst = nullptr;
              if ( (callInst = dyn_cast<CallInst>(SecUserlist[iii])) ) {
                Function * f = callInst->getCalledFunction();
                if (f) {
                  bool found = false, Mempcmp = false;
                  found |= !f->getName().compare(StringRef("strcmp")) || !f->getName().compare(StringRef("strcasecmp"));
                  found |= !f->getName().compare(StringRef("strncmp")) || !f->getName().compare(StringRef("strncasecmp"));
                  Mempcmp |= !f->getName().compare(StringRef("memcmp"));
                  found |= Mempcmp;
                  // TODO: check function type
                  // TODO convention call
                  if (found) {
                    // ArrayType * AT = 0;
                    // if ( (AT = dyn_cast<ArrayType>(CstInit->getType())) ) {
                    // ASSERT ( AT->getElementType()->getPrimitiveSizeInBits() == 8 );
                    
                    /* Get the string data and ignore */
                    StringRef ConstStr;
                    bool HasStr = getConstantStringInfo(src, ConstStr, 0, Mempcmp ? false : true);
                    ASSERT (HasStr); // it's fed into memcpy, it'd better be a string

                    std::string KeyWord(ConstStr.data(), ConstStr.size());
                    std::string debugInfo = utils::getDebugInfo(*CI);
                    utils::DictElt elmt = utils::DictElt(KeyWord, debugInfo);
                    addToDictionary(*callInst, elmt, dictAdded);
                    //errs() << "added2:" << *callInst << "\n";
                    // if ( !dict.count(elmt) ) {
                    //   dict.insert(elmt);
                    // }
                    // size_t & dictAdded
                    ++dictAdded;
                  }
                }
              }
              // std::vector<llvm::Instruction*> ThirdUserlist;
              // getUsersOf(*SecUserlist[iii], ThirdUserlist);
              // for ( size_t j=0; j<ThirdUserlist.size(); ++j ) {
              //   errs() << "user3:" << *ThirdUserlist[j] << "\n";
              // }
                
            }              
          }
        }
      }      
    }
  }
}


bool StrCompare2Unit::runOnModule(Module &M) {

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

    SAYF(cCYA "strcompare-to-unit pass " cBRI VERSION cRST "\n");
    
    OKF("AFL_CONVERT_COMPARISON_TYPE = %s", utils::getEnvVar("AFL_CONVERT_COMPARISON_TYPE"));

  } else be_quiet = 1;


  unsigned Strcmp = 0, Strncmp = 0, Memcmp = 0, BothVariable = 0;
  size_t dictAdded = 0;
  /* Creating the custon kinds seems necessary. Without this I encountered problems when adding metadata */
  getGlobalContext().getMDKindID(S2U_DICT); /* create the S2U_DICT meta kind */

  harvestConstantArrays(M, dictAdded);
  harvestConstantStores(M, dictAdded);
  harvestConstantStructs(M, dictAdded);

  transformCmps(M, Strcmp, Strncmp, Memcmp, BothVariable, dictAdded);

  //verifyModule(M);

  /* Update the file containing dictionary */
  #if 0
  if ( Dictionary.size() ) {
    char* dict_file = getenv("AFL_BCCLANG_DICT_FILE");
    if (!dict_file) {
      FATAL("AFL_BCCLANG_DICT_FILE not defined");
    }

    std::fstream outfs;
    outfs.open(dict_file, std::fstream::out|std::fstream::app);
    ASSERT ( outfs.is_open() );
    u32 n = 1;
    for (auto & elt : Dictionary) {
      std::string ss = utils::Stringify(elt.getFirst());
      std::string debug = elt.getSecond();
      if (ss.size())
        outfs << "S2U_" << std::setw(8) << std::setfill('0') << std::hex << n++ << "_" << (debug.size()?debug:"NDEBUG") << "=\"" << ss << "\"\n";
    }
  }
  #endif

  if (!be_quiet) {

    if (!Strcmp) WARNF("No instrumentation STRCMP found.");
    else OKF("Instrumented %u STRCMP.", Strcmp);

    if (!Strncmp) WARNF("No instrumentation STRNCMP found.");
    else OKF("Instrumented %u STRNCMP.", Strncmp);

    if (!Memcmp) WARNF("No instrumentation MEMCMP found.");
    else OKF("Instrumented %u MEMCMP.", Memcmp);

    if (BothVariable)
      OKF("Instrumented %u variable-only compare(s).", BothVariable);

    if (!dictAdded) WARNF("No entries added to DICT.");
    else OKF("Added %zu entries to DICT.", dictAdded);
  }

  return true;
}

static void registerCompTransPass(const PassManagerBuilder &,
                            legacy::PassManagerBase &PM) {

  auto p = new StrCompare2Unit();
  PM.add(p);

}

static RegisterStandardPasses RegisterCompTransPass(
    PassManagerBuilder::EP_OptimizerLast, registerCompTransPass);

static RegisterStandardPasses RegisterCompTransPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0, registerCompTransPass);

