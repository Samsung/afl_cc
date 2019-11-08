/*
   american fuzzy lop - LLVM-mode instrumentation pass
   ---------------------------------------------------

   Written by Laszlo Szekeres <lszekeres@google.com> and
              Michal Zalewski <lcamtuf@google.com>

   LLVM integration design comes from Laszlo Szekeres. C bits copied-and-pasted
   from afl-as.c are Michal's fault.

   Copyright 2015, 2016 Google Inc. All rights reserved.

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at:

     http://www.apache.org/licenses/LICENSE-2.0

   This library is plugged into LLVM when invoking clang through afl-clang-fast.
   It tells the compiler to add code roughly equivalent to the bits discussed
   in ../afl-as.h.

 */

#define AFL_LLVM_PASS

#include "../config.h"
#include "../debug.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <utility>
#include <set>
#include <fstream>

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/CFG.h"

#include "afl-llvm-pass-parent.h"


#if LLVM_VERSION_CODE > LLVM_VERSION(4, 0)
#include "llvm/Support/Error.h"
#endif

#define AFL_BB_NAME "AFL."

using namespace llvm;

#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
static LLVMContext TheContext;
#endif

namespace {

  class AFLCoverage : public AFLPassParent, public ModulePass {

    public:

      static char ID;
      AFLCoverage() : ModulePass(ID) { }

      bool runOnModule(Module &M) override;

      //void getAnalysisUsage(AnalysisUsage &Info) const;
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      LLVMContext & getGlobalContext(void) {
        return TheContext;
      }
#endif

    private:
      typedef std::set< std::pair <TerminatorInst *, unsigned> > InstructionsSet_t;

      void recordDictToEdgeMappings(BasicBlock & srcBB, BasicBlock & dstBB, utils::Dict2_t & dict, u32 & edge_id);
      void recordInstruction( TerminatorInst & BI, InstructionsSet_t & InstSet);
      void recordSrcInformation(TerminatorInst & TI, unsigned idx, u32 edge_count, CoverageInfo_t & coverageInfo);
      void instrumentBasicBlock(Instruction &I, GlobalVariable *AFLMapPtr, u32 edge_id);
      void instrumentInstruction(TerminatorInst & TI, unsigned idx, GlobalVariable *AFLMapPtr, u32 edge_id, utils::Dict2_t & dict);
      void splitLandingPadPreds(Function & F);
      void setLandingPadsWithUniquePredecessor(Module & M);

#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      StringRef getPassName() const override {
#else
      const char* getPassName() const override {
#endif
       return "American Fuzzy Lop Instrumentation";
      }

  };

}


char AFLCoverage::ID = 0;


void AFLCoverage::instrumentBasicBlock(Instruction &I, GlobalVariable *AFLMapPtr, u32 edge_id) {
  
  LLVMContext &C = getGlobalContext();
  IntegerType *Int32Ty = IntegerType::getInt32Ty(C);
  IntegerType *Int8Ty  = IntegerType::getInt8Ty(C);

  ConstantInt *CurEdge32 = ConstantInt::get(Int32Ty, edge_id);
  Module &M = *I.getParent()->getParent()->getParent();
  IRBuilder<> IRB(&I);

  /* Load SHM pointer */

  LoadInst *MapPtr = IRB.CreateLoad(AFLMapPtr);
  MapPtr->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
  Value *MapPtrIdx = IRB.CreateGEP(MapPtr, CurEdge32);

  /* Update bitmap */

  LoadInst *Counter = IRB.CreateLoad(MapPtrIdx);
  Counter->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
  Value *Incr = IRB.CreateAdd(Counter, ConstantInt::get(Int8Ty, 1));
  IRB.CreateStore(Incr, MapPtrIdx)
      ->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));  

}


void AFLCoverage::recordInstruction( TerminatorInst & I, InstructionsSet_t & InstSet) {
  unsigned NbSuccessors = I.getNumSuccessors();
  for (unsigned idx=0; idx< NbSuccessors; ++idx) {
    
    //errs() << "added " << idx << " " << I << "\n";
    InstSet.insert( std::make_pair( &I, idx )  );
    
  }
}

void AFLCoverage::recordDictToEdgeMappings(BasicBlock & srcBB, BasicBlock & dstBB, utils::Dict2_t & dict, u32 & edge_id) {
  
  //ASSERT (&srcBB != &dstBB);
  recordDictToEdgeMapping(srcBB, dict, edge_id);
  if (&srcBB != &dstBB) {
    recordDictToEdgeMapping(dstBB, dict, edge_id);
  }
}


void AFLCoverage::instrumentInstruction( TerminatorInst & TI, unsigned idx, GlobalVariable *AFLMapPtr, u32 edge_id, utils::Dict2_t & dict) {

  LLVMContext &C = getGlobalContext();
  BasicBlock * BBSuccessor = TI.getSuccessor(idx); ASSERT (BBSuccessor);
  BasicBlock * TIBB = TI.getParent();

  /* record dict <-> edge mapping */
  recordDictToEdgeMappings(*TIBB, *BBSuccessor, dict, edge_id);

  /* Warning:  getSinglePredecessor != getUniquePredecessor, eg in switch statement 
               see http://llvm.org/doxygen/BasicBlock_8h_source.html#l00224
  */
  if ( BasicBlock * Pred = BBSuccessor->getSinglePredecessor() ) {
    
    /* sanity checks */
    ASSERT ( Pred == TIBB );
    StringRef BBName = BBSuccessor->getName();
    if ( BBName.find(AFL_BB_NAME) != StringRef::npos ) ASSERT (0 && "Unique successor already instrumented");

    /* Add instrumentation at the start of the block instead */
    BasicBlock::iterator IP = BBSuccessor->getFirstInsertionPt();

    /* Update the name of BB as currentName_Afl_n */
    BBSuccessor->setName( Twine(AFL_BB_NAME) + Twine(BBName == "" ? "NoName" : BBName) + Twine(".") + Twine(edge_id) );

    instrumentBasicBlock(*IP, AFLMapPtr, edge_id);


  } else {

    /* Create new BB with name Afl_n */
    BasicBlock * NewBB = BasicBlock::Create(C, Twine(AFL_BB_NAME) + Twine("New.") + Twine(edge_id), TI.getParent()->getParent(), 
                                BBSuccessor /* Insert before the successor */); ASSERT (NewBB && "NewBB is null");    
    IRBuilder<> builder(NewBB);

    /* Set the current TI's successor to our NewBB */
    TI.setSuccessor(idx, NewBB);

    /* Insert the instrumentation in this NewBB */
    BranchInst * NewTI = builder.CreateBr(BBSuccessor); ASSERT(NewTI);

    /* Now that the BB is initialized, instrument it */
    instrumentBasicBlock(*NewTI, AFLMapPtr, edge_id);

    for ( auto & II : *BBSuccessor ) {
      if ( isa<PHINode>(II) ) {
        PHINode & PHI = cast<PHINode>(II);

        PHI.setIncomingBlock ( PHI.getBasicBlockIndex(TIBB), NewBB);
        
      }
    }
  }
}

void AFLCoverage::recordSrcInformation(TerminatorInst & TI, unsigned idx, u32 edge_count, CoverageInfo_t & coverageInfo) {
  
  /* Record src info */
  // BasicBlock * EntryBB = &(*TI.getParent()->getParent()->begin());
  std::string srcInfo = utils::getSrcInfo(*TI.getParent()) + ",";
  
  /* get dst info */
  srcInfo = utils::addSrcInfo(*TI.getSuccessor(idx), srcInfo); 
  
  std::pair<unsigned, std::string> pair = std::make_pair(edge_count, srcInfo);
  ASSERT ( !coverageInfo.count(pair) );
  coverageInfo.insert(pair);
}

// Inspired from lib/Transforms/IPO/LoopExtractor.cpp in LLVM 3.8
void AFLCoverage::splitLandingPadPreds(Function & F) {
 
  std::set<BasicBlock *> BBSet;
  for (auto & BB : F) {
    if ( BB.isLandingPad() ) {
      if ( BB.getSinglePredecessor() == 0 ) {
        /* Multiple predecessor */
        for (auto it = pred_begin(&BB), et = pred_end(&BB); it != et; ++it) {
          BasicBlock* Pred = *it;
          ASSERT ( isa<InvokeInst>(Pred->getTerminator()) );
          ASSERT ( !BBSet.count(Pred) ); /* Should not already be recorded */
          BBSet.insert(Pred);
        }
      }
    }
  }

  for (auto Pred : BBSet) {
    SmallVector<BasicBlock*, 2> NewBBs;
    SplitLandingPadPredecessors(cast<InvokeInst>(Pred->getTerminator())->getUnwindDest(), Pred, ".1", ".2", NewBBs);
  }
}

void AFLCoverage::setLandingPadsWithUniquePredecessor(Module & M) {
  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    splitLandingPadPreds(*F);
  }
}

bool AFLCoverage::runOnModule(Module &M) {

  LLVMContext &C = M.getContext();

  IntegerType *Int8Ty  = IntegerType::getInt8Ty(C);
  CoverageInfo_t coverageInfo;

  /* Show a banner */

  char be_quiet = 0;

  if (isatty(2) && !getenv("AFL_QUIET")) {

    SAYF(cCYA "afl-llvm-pass " cBRI VERSION cRST " (NO_COLLISION)\n");

  } else be_quiet = 1;

  /* Decide instrumentation ratio */

  // char* inst_ratio_str = getenv("AFL_INST_RATIO");
  // unsigned int inst_ratio = 100;

  // if (inst_ratio_str) {

  //   if (sscanf(inst_ratio_str, "%u", &inst_ratio) != 1 || !inst_ratio ||
  //       inst_ratio > 100)
  //     FATAL("Bad value of AFL_INST_RATIO (must be between 1 and 100)");

  // }

  DICT_TYPE dictType = getDictType();
  BUILD_TYPE buildType = getBuildType();
  bool isCoverageBuild = (buildType == BUILD_COVERAGE);
  
  if ( utils::isEnvVarSet("AFL_OPTIMIZATION_ON") ) {
    FATAL("Optimization not supported. Aborting.");
    return false;
  }

  if (isCoverageBuild) {
    FATAL("Cannot use NO_COLLISION pass for coverage build");
  }

  /* Should not be used... */
  if (isCoverageBuild) {
    srandom(*((unsigned int *)"coverage"));
  }

  /* Get globals for the SHM region and the previous location. Note that
     __afl_prev_loc is thread-local. */

  GlobalVariable *AFLMapPtr =
      new GlobalVariable(M, PointerType::get(Int8Ty, 0), false,
                         GlobalValue::ExternalLinkage, 0, "__afl_area_ptr");
  
  InstructionsSet_t ISet;
  utils::Dict2_t dict;
  u32 edge_count = 1;

  /* make all landing pads have a unique predecessor */
  setLandingPadsWithUniquePredecessor(M);

  for (auto &F : M) {
    for (auto &BB : F) {

      //errs() << "BB:" << BB.getName() << "\n";
      //BB.dump();

      //if ( BB.getName().startswith(AFL_BB_NAME) ) { continue; /* Already instrumented */ }

      // now enumerate all successors
      TerminatorInst * TI = BB.getTerminator();

      /* If there's a dictionary value recorded, we instrument the code even if it's not a conditional branch
         This allows us to have a mapping between dict value and edge did
       */
      bool has_dict = isDictRecordedToBB(BB);
      if ( !isCoverageBuild && has_dict ) {
        /* Record the instruction */
        recordInstruction(*TI, ISet);

      } else {

        // TODO: question: memcpy -> what happens? instrumented or not?? It it is llvm.memcpy so not instrumented?
        if ( isa<ReturnInst>(TI) ) { 

          continue; 

        } else if ( isa<UnreachableInst>(TI) ) { // TODO: taek a closer look at this: the preceding instruction may be the one we care about
          
          continue; 

        } else if ( isa<BranchInst>(TI) ) {

          BranchInst * BI = cast<BranchInst>(TI);
          
          if ( BI->isUnconditional() ) continue;

        } else if ( isa<InvokeInst>(TI) ) {
          // nothing to do
          InvokeInst & II = *cast<InvokeInst>(TI);
          // errs() << II << "\n";
          // errs() << *II.getNormalDest() << "\n";
          // errs() << *II.getUnwindDest() << "\n";
          /* We should have  single predecessor, as we called setLandingPadsWithUniquePredecessor() */
          ASSERT ( II.getUnwindDest()->getSinglePredecessor() != NULL );// ASSERT (0);
          //ASSERT(0);

        } else if ( isa<ResumeInst>(TI) ) {
          
          ASSERT ( 0 == TI->getNumSuccessors() );
          continue; /* no need to instrument */

        } else if ( isa<SwitchInst>(TI) ) {

          /* nothing to do */
          ASSERT (0 && "Found switch instruction. Should be removed it passed -lowerswitch to opt");

        } else {
          errs() << "UNSUPPORTED Terminator:" << *TI << "\n";
          errs() << "F:" << *TI->getParent()->getParent() << "\n";
          exit(-1);
        }

        /* Record the instruction */
        recordInstruction(*TI, ISet);
      }
    }
  }

  /* Instrumentation */
  for(auto &Elt : ISet) {
    //errs() << "instrumented\n";
    TerminatorInst * TI = Elt.first;
    unsigned idx = Elt.second;

    /* Record src info, before we change the flow */
    if ( isCoverageBuild ) {
      recordSrcInformation(*TI, idx, edge_count, coverageInfo);
    }

    /* Instrument the instruction */
    instrumentInstruction(*TI, idx, AFLMapPtr, edge_count, dict);
    
    ++edge_count;

  }

  /* Set the size of the areas */
  createAreaSizeFunction(M, edge_count);
  OKF("Edge Map size used: %u KB", edge_count/1024);

  createBBAreaSizeFunction(M, edge_count);

  /* Say something nice. */

  if (!be_quiet) {

    if (!(edge_count>1)) WARNF("No instrumentation targets found.");
    else OKF("Instrumented %u edges (%s mode).",
             edge_count-1, getenv("AFL_HARDEN") ? "hardened" :
             ((getenv("AFL_USE_ASAN") || getenv("AFL_USE_MSAN")) ?
              "ASAN/MSAN" : "non-hardened"));
    /* Note: I don't display the number of dictionary elements, 
      as those are already displayed by previous passes, 
      ie here we just retrieve them and write them to file*/
  }


  /* Create the file containing # edges */
  writeMapSizeToFile(edge_count);

  /* Create file containing # BB, which is the same here since we use edges... */
  writeBBSizeToFile(edge_count);
  
  /* Create a unique ID for the build (8 bytes), and save to file */
  uint64_t buildID = generateBuildID();
  writeBuildIDToFile(buildID);

  /* Update the file containing dictionary */
  writeDictToFile(dict, buildID, buildType, dictType);
  
  /* create the file with edge ID <-> source code mapping */
  if ( isCoverageBuild ) {
    writeSrcToEdgeMappingToFile(coverageInfo);
  }

  return true;

}

// void AFLCoverage::getAnalysisUsage(AnalysisUsage &AU) const {
//   //AU.setPreservesCFG();
//   AU.addRequired<RegToMem>();
// }

static void registerAFLPass(const PassManagerBuilder &,
                            legacy::PassManagerBase &PM) {

  PM.add(new AFLCoverage());
  // AU.addRequired<PassName>();
  // PassName &P = getAnalysis<PassName>();
}


static RegisterStandardPasses RegisterAFLPass(
    PassManagerBuilder::EP_OptimizerLast, registerAFLPass);

static RegisterStandardPasses RegisterAFLPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0, registerAFLPass);
