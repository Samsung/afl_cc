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
#include <fstream>

#include "afl-llvm-pass-parent.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Analysis/BasicAliasAnalysis.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/CFG.h"

using namespace llvm;

/* -mllvm -map-size=N */
static cl::opt<unsigned> MapSize("map-size", cl::desc("Size of map (KB)"), cl::init(0));

namespace {

  class AFLCoverage : public AFLPassParent, public ModulePass {

    private:
      std::set<std::string> PointedToFunctions;
      bool PointedToEnabled;

      void recordSrcInformation(BasicBlock & BB, unsigned bb_id, CoverageInfo_t & coverageInfo);
      bool skipEdge(BasicBlock & BB, Function & F) const;
      void readPointedToFunctions(Module & M);
      unsigned calculateMapSize(Module & M, BUILD_TYPE buildType) const;

    public:

      static char ID;
      AFLCoverage() : ModulePass(ID) { }

      bool runOnModule(Module &M) override;
      
#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
      StringRef getPassName() const override {
#else
      const char* getPassName() const override {
#endif
       return "American Fuzzy Lop Instrumentation";
      }

  };

}

static inline bool starts_with(const std::string& str, const std::string& prefix)
{
  if(prefix.length() > str.length()) { return false; }
  return str.substr(0, prefix.length()) == prefix;
}

#include <iostream>
static inline bool is_llvm_dbg_intrinsic(Instruction& instr)
{
  const bool is_call = instr.getOpcode() == Instruction::Invoke ||
                       instr.getOpcode() == Instruction::Call;
  if(!is_call) { return false; }

  CallSite cs(&instr);
  Function* calledFunc = cs.getCalledFunction();

  if (calledFunc != NULL) {
    const bool ret = calledFunc->isIntrinsic() &&
                     starts_with(calledFunc->getName().str(), "llvm.");
    return ret;
  } else { 
    return false;
  }
}

void AFLCoverage::recordSrcInformation(BasicBlock & BB, unsigned bb_id, CoverageInfo_t & coverageInfo) {
  
  /* Record info */
  std::string srcInfo = utils::getSrcInfo(BB);
  
  /* There may be no source code for the BB, but we still want to record that the BB is valid 
     This allows us to sanity check the BBs we find when we extract metrics
  */
  std::pair<unsigned, std::string> pair = std::make_pair(bb_id, srcInfo);
  std::set< std::pair<unsigned, std::string> >::iterator it = coverageInfo.find(pair);
  ASSERT ( !coverageInfo.count(pair) );
  coverageInfo.insert(pair);
   
}

bool AFLCoverage::skipEdge(BasicBlock & BB, Function & F) const {

  
  TerminatorInst * TI = BB.getTerminator();

  //errs() << "BB:" << BB << "\n";

  /* Perform some sanity checks */
  ASSERT(isa<ReturnInst>(TI) || isa<UnreachableInst>(TI) || isa<BranchInst>(TI) || isa<InvokeInst>(TI) || isa<ResumeInst>(TI));

  /* 1. if this is the first BB of a function, skip ege it unless it's pointed to by a function pointer or 
  thru polimorphism */
  if (PointedToEnabled && &BB == &F.getEntryBlock()) {
    
    //errs() << "first BB of " << F.getName() << "\n";
    if (!PointedToFunctions.size()) return true;

    return !PointedToFunctions.count(F.getName());
  }

  /* 2. If all predecessors branch unconditionally to this BB, skip edge */
  bool SkipEdge = true;
  unsigned NbPredecessors = 0;
  for (BasicBlock * Pred : predecessors(&BB)) {
    //errs() << " Pred:" << *Pred << "\n";
    SkipEdge &= (isa<BranchInst>(Pred->getTerminator()) && cast<BranchInst>(Pred->getTerminator())->isUnconditional());
    ++NbPredecessors;
  }

  SkipEdge &= !!NbPredecessors;

  return SkipEdge;
}

void AFLCoverage::readPointedToFunctions(Module & M) {
  Function * Main = M.getFunction("main"); ASSERT(Main);
  MDNode* Meta = Main->getMetadata("FuncPointedToList");
  /* This is populated by dsa/lib/DSA/CallTargets.cpp, but currently disabled in afl-make-bc */
  if (Meta) {
    OKF("Using DSA analysis for function pointers");
    PointedToEnabled = true;

    const std::string listStr = cast<MDString>(Meta->getOperand(0))->getString().str();
    std::istringstream f(listStr);
    std::string fname;    
    while (getline(f, fname, ' ')) {
        PointedToFunctions.insert(fname);
    }
  }
}

unsigned AFLCoverage::calculateMapSize(Module & M, BUILD_TYPE buildType) const {
  /* Get an upper bound on number of edges
    - successor, predecessors
    - calls/invokeinst

    m = max(#succ,#prede) where succ for a call is 1 if direct, 20 else. We could use point-to analysis but we need a fallback number anyway...
    the sizemap = (#BB * m)^4, such that sizemap^(1/4) = upper-bound-n-edges

    Unfortunately, this leads to huge huge numbers... so instead I just use the max integer "possible"
  */
  
  bool isCoverageBuild = (buildType == BUILD_COVERAGE);
  #define MaxSize (((unsigned)(-1) - 8)/1024) /* This is used to minimize edge collision */

  /* perform sanity checks */
  if (!(MapSize <= MaxSize/1024)) {
    FATAL("Overflow -map-size. Max value accepted is %u", MaxSize/1024);
  }

  /* coverage build: we want a minimum size */
  if (isCoverageBuild) {
    return MaxSize;
  }
  
  /* not coverage build: use whatever the user asked, or the default value */
  return MapSize ? MapSize*1024 : MAP_SIZE;
}

char AFLCoverage::ID = 0;

bool AFLCoverage::runOnModule(Module &M) {

  LLVMContext &C = M.getContext();

  IntegerType *Int8Ty  = IntegerType::getInt8Ty(C);
  IntegerType *Int32Ty = IntegerType::getInt32Ty(C);
  Type *VoidType = Type::getVoidTy(C);

  CoverageInfo_t coverageInfo;

  /* Show a banner */

  char be_quiet = 0;

  if (isatty(2) && !getenv("AFL_QUIET")) {

    SAYF(cCYA "afl-llvm-pass " cBRI VERSION cRST " (ORIGINAL)\n");

  } else be_quiet = 1;

  BUILD_TYPE buildType = getBuildType();
  bool isCoverageBuild = (buildType == BUILD_COVERAGE);
  DICT_TYPE dictType = isCoverageBuild ? DICT_NORMAL : getDictType();
  bool isDictOptimized = (dictType == DICT_OPTIMIZED);


  /* Read function for which we need the original instrumentation */
  readPointedToFunctions(M);

  /* Decide instrumentation ratio */

  char* inst_ratio_str = getenv("AFL_INST_RATIO");
  unsigned int inst_ratio = 100;

  // if (inst_ratio_str) {

  //   if (sscanf(inst_ratio_str, "%u", &inst_ratio) != 1 || !inst_ratio ||
  //       inst_ratio > 100)
  //     FATAL("Bad value of AFL_INST_RATIO (must be between 1 and 100)");

  // }

  if (isCoverageBuild) {
    srandom(*((unsigned int *)"coverage"));
  }

  /* Get globals for the SHM region and the previous location. Note that
     __afl_prev_loc is thread-local. */

  GlobalVariable *AFLMapPtr =
      new GlobalVariable(M, PointerType::get(Int8Ty, 0), false,
                         GlobalValue::ExternalLinkage, 0, "__afl_area_ptr");

  GlobalVariable *AFLPrevLoc = new GlobalVariable(
      M, Int32Ty, false, GlobalValue::ExternalLinkage, 0, "__afl_prev_loc",
      0, GlobalVariable::GeneralDynamicTLSModel, 0, false);

  Function *BBTraceFunc = 0;

  /* We need to record the BB ids 
    1. to map them to a dictionary word, if dictionary is optimized 
    2. to map them to source line, if it's a coverage build
  */
  if (isDictOptimized || isCoverageBuild) {
    BBTraceFunc = Function::Create(FunctionType::get(VoidType, Int32Ty, false), GlobalValue::ExternalLinkage, "__afl_bb_trace", &M);
  }

  /* Instrument all the things! */

  unsigned inst_blocks = 0;
  u32 map_size = calculateMapSize(M, buildType);
  utils::Dict2_t dict;
  
  for (auto &F : M)
    for (auto &BB : F) {

      BasicBlock::iterator IP = BB.getFirstInsertionPt();
      IRBuilder<> IRB(&(*IP));

      /* Don't instrument the BB if not needed */
      bool SkipEdge = isCoverageBuild && skipEdge(BB, F);

      //if (AFL_R(100) >= inst_ratio) continue;

      /* Make up cur_loc */

      unsigned int cur_loc = AFL_R(map_size);
      
      ConstantInt *CurLoc = ConstantInt::get(Int32Ty, cur_loc);
      /* We use a different BB id for coverage, else we run into birthday paradox and collisions occur. 
        Collisions are bad because we cannot uniquely map them to a unique line of the source code.
        So I use a counter to avoid collision. I do *not* use it for calculating edges: we keep
        AFL's original code for this, ie using random BB ids.
        Although we can accomodate some collision for optimized dictionary builds, I re-use the same code...
        */
      ConstantInt * BlockID = ConstantInt::get(Int32Ty, inst_blocks);

      /* For coveraeg build, we reset the prevLoc after each call instruction.
         This ensures that if there are multiple returns in the callee, we don't create
         non-fonctional edges between the current BB and a successor

         Do this before the rest of the instrumentation, as we need to reset prevLoc
         after BBTraceFunc

         We msut split landing pad with a unique precessor, and we can add the reset curloc at the start of landing pad!
         we must also exit early if we're dealing with a landing pad BB

         WARNING: we no longer do this: instead we use -mergereturn in afl-make-bc. It's much simpler
       */

      /* Call trace BBTraceFunc function */
      if (BBTraceFunc) {

        /* Update the BB trace coverage map */
        IRB.CreateCall(BBTraceFunc, BlockID);

        /* Record mapping BB (BBid <-> {dict or src}) */
        recordDictToEdgeMapping(BB, dict, inst_blocks);

      } else if (!isDictOptimized && !isCoverageBuild) {

        /* Record the dictionary value. inst_blocks will not be used by AFL anyway in this case */
        recordDictToEdgeMapping(BB, dict, inst_blocks);
      }

      /* Record line of source code */
      if (isCoverageBuild) {
        recordSrcInformation(BB, inst_blocks, coverageInfo);
      }

      /* Load prev_loc */

      LoadInst *PrevLoc = IRB.CreateLoad(AFLPrevLoc);
      PrevLoc->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));

      /* Update the bitmap only if we must */
      if (!SkipEdge) {

        Value *PrevLocCasted = IRB.CreateZExt(PrevLoc, IRB.getInt32Ty());


        /* Load SHM pointer */

        LoadInst *MapPtr = IRB.CreateLoad(AFLMapPtr);
        MapPtr->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        Value *MapPtrIdx =
            IRB.CreateGEP(MapPtr, IRB.CreateXor(PrevLocCasted, CurLoc));

        /* Update bitmap */

        LoadInst *Counter = IRB.CreateLoad(MapPtrIdx);
        Counter->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
        Value *Incr = IRB.CreateAdd(Counter, ConstantInt::get(Int8Ty, 1));
        IRB.CreateStore(Incr, MapPtrIdx)
            ->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
      } 

      /* Set prev_loc to cur_loc >> 1 */

      StoreInst *Store =
          IRB.CreateStore(ConstantInt::get(Int32Ty, cur_loc >> 1), AFLPrevLoc);
      Store->setMetadata(M.getMDKindID("nosanitize"), MDNode::get(C, None));
      
      inst_blocks++;

    }


  /* Set the size of the area. We could change it dynamically... */
  createAreaSizeFunction(M, map_size);
  OKF("Edge Map size used: %u KB", map_size/1024);

  createBBAreaSizeFunction(M, inst_blocks);

  /* Say something nice. */

  if (!be_quiet) {

    if (!inst_blocks) WARNF("No instrumentation targets found.");
    else OKF("Instrumented %u locations (%s mode, ratio %u%%).",
             inst_blocks, getenv("AFL_HARDEN") ? "hardened" :
             ((getenv("AFL_USE_ASAN") || getenv("AFL_USE_MSAN")) ?
              "ASAN/MSAN" : "non-hardened"), inst_ratio);

  }

  /* Create the file containing # edges */
  writeMapSizeToFile(map_size);

  /* Create file containing # BB */
  writeBBSizeToFile(inst_blocks);
  

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


static void registerAFLPass(const PassManagerBuilder &,
                            legacy::PassManagerBase &PM) {
  PM.add(new AFLCoverage());
}


static RegisterStandardPasses RegisterAFLPass(
    PassManagerBuilder::EP_OptimizerLast, registerAFLPass);

static RegisterStandardPasses RegisterAFLPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0, registerAFLPass);
