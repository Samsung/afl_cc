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

#include "utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fstream>
#include <iomanip>
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
#include "llvm/IR/IntrinsicInst.h"

#include "afl-llvm-pass-parent.h"

#define S2U_BB_NAME "SIC"
#define MAX_DICT_MULTIPLE 20

using namespace llvm;

#if LLVM_VERSION_CODE > LLVM_VERSION(3, 8)
static LLVMContext TheContext;
#endif

/* -mllvm -fignore-strings-to=func1,func2 */
static cl::opt<std::string> ListFuncNameToIgnore("fignore-strings-to", cl::desc("Ignore string arguments to these functions"));

namespace {

  class StrInCalls : public AFLPassParent, public ModulePass {

    public:
      static char ID;
      StrInCalls() : ModulePass(ID) {
      } 

      std::set<std::string> FuncNameSet;
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
        return "grab strings from calls";
      }

    private:
      void grabStringsInCalls(Module &M, size_t & dictAdded);
     
  };
}


char StrInCalls::ID = 0;


void StrInCalls::grabStringsInCalls(Module &M, size_t & dictAdded) {

  
  for (auto &F : M) {
    for (auto &BB : F) {
      for(auto &IN: BB) {

        CallInst* CI = nullptr;
        
        if ( isa<IntrinsicInst>(&IN) ) { continue; }

        if (!(CI = dyn_cast<CallInst>(&IN))) { continue; }

        /* Discard functions we don't care about */
        Function * fun = CI->getCalledFunction();
        if ( !fun ) { continue; }
        
        StringRef FuncName = utils::demangleName(fun->getName());
        
        /* A bunch of heuristics to reduce the number of useless strings... */
        if ( utils::Tolower(FuncName.str()).find("assert") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("abort") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("err") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("warn") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("debug") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("fatal") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("strcat") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("strncat") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("strcpy") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("strncpy") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("append") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("prepend") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("stddup") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("write") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("print") != std::string::npos ) { continue; }
        if ( utils::Tolower(FuncName.str()).find("memcpy") != std::string::npos ) { continue; }
        if ( FuncName == "puts" || FuncName == "fputs" ) { continue; }
        if ( FuncNameSet.count(FuncName.str()) ) { continue; }

        if ( FuncName == "getenv" ) {
          StringRef Str;
          if ( getConstantStringInfo(CI->getArgOperand(0), Str) ) {
            WARNF("Environment variable '%s' used", Str.data());
          } else {
            WARNF("(Non-constant?) Environment variable used");
          }
          continue; /* No need to use this as dictionary */
        }
        
        if (CI->getCallingConv() == llvm::CallingConv::C) {
          /* These are already taken care of by strcompare-to-unit pass */
          bool isStrcmp = !FuncName.compare(StringRef("strcmp")) || !FuncName.compare(StringRef("strcasecmp"));
          bool isStrncmp = !FuncName.compare(StringRef("strncmp")) || !FuncName.compare(StringRef("strncasecmp"));
          bool isMemcmp = !FuncName.compare(StringRef("memcmp"));
          if ( isStrcmp || isStrncmp || isMemcmp ) { continue; }
        }

        for (unsigned i=0; i<CI->getNumOperands(); ++i) {
          auto op = CI->getArgOperand(i);

          StringRef Str;
          if ( getConstantStringInfo(op, Str, 0, false) && Str.size() ) {
            std::string KeyWord(Str.data(), Str.size());
            std::string debugInfo = utils::getDebugInfo(*CI);
            utils::DictElt elmt = utils::DictElt(KeyWord, debugInfo);
            utils::recordMultipleDictToInstr(getGlobalContext(), *CI, elmt, SIC_DICT, true, true);
            //errs() << "added:" << *CI << "\n";
            ++dictAdded;
            // if ( !dict.count(elmt) ) {
            //   dict.insert(elmt);
            // }
          }
        }        
      }      
    }
  }
}


bool StrInCalls::runOnModule(Module &M) {

  /* Show a banner */

  char be_quiet = 0;

  if (isatty(2) && !getenv("AFL_QUIET")) {

    SAYF(cCYA "strings-in-call pass " cBRI VERSION cRST "\n");

  } else be_quiet = 1;

  size_t dictAdded = 0;
  
  utils::split(ListFuncNameToIgnore, ',', FuncNameSet, true /* demangle */);

  grabStringsInCalls(M, dictAdded);

  //verifyModule(M);

#if 0
  /* Update the file containing dictionary */
  if ( Dictionary.size() ) {
    char* dict_file = getenv("AFL_BCCLANG_DICT_FILE");
    if (!dict_file) {
      FATAL("AFL_BCCLANG_DICT_FILE not defined");
    }

    std::fstream outfs;
    outfs.open(dict_file, std::fstream::out);
    ASSERT ( outfs.is_open() );
    //u32 n = 1;
    for (auto & elt : Dictionary) {
      std::string ss = utils::Stringify(elt.getFirst());
      std::string debug = elt.getSecond();
      if (ss.size())
        outfs << "SIC_" /*<< std::setw(8) << std::setfill('0') << std::hex << n++ << "_"*/ << (debug.size()?debug:"NDEBUG") << "=\"" << ss << "\"\n";
    }
  }
#endif

  if (!be_quiet) {

    if (!dictAdded) WARNF("No entries added to DICT.");
    else OKF("Added %zu entries to DICT.", dictAdded);

  }

  return true;
}

static void registerCompTransPass(const PassManagerBuilder &,
                            legacy::PassManagerBase &PM) {

  auto p = new StrInCalls();
  PM.add(p);

}

static RegisterStandardPasses RegisterCompTransPass(
    PassManagerBuilder::EP_OptimizerLast, registerCompTransPass);

static RegisterStandardPasses RegisterCompTransPass0(
    PassManagerBuilder::EP_EnabledOnOptLevel0, registerCompTransPass);

