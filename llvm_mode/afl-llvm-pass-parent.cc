#include "afl-llvm-pass-parent.h"

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
#include "llvm/Support/Debug.h"
#include "llvm/Transforms/IPO/PassManagerBuilder.h"
#include "llvm/IR/CFG.h"

using namespace llvm;


AFLPassParent::AFLPassParent() {
  utils::init_metanames();
}


uint64_t AFLPassParent::generateBuildID(void) {
  uint64_t ret = 0;
  size_t i;
  for( i = 0 ; i < sizeof(ret); ++i ) {
    ((uint8_t *) &ret)[i] = rand() % 256;
 }
 return ret;
}

void AFLPassParent::_writeSizeToFile(uint32_t map_size, const char * env) {
  char* edge_file = getenv(env);
  if (!edge_file) {
    FATAL("%s not defined", env);
  }

  std::ifstream infile(edge_file);
  if ( infile.good() ) {
    FATAL("File %s already exists", edge_file);
  }
  infile.close();
  
  std::fstream outfs;
  outfs.open(edge_file, std::fstream::out|std::fstream::binary);
  ASSERT ( outfs.is_open() );
  outfs.write((char*)&map_size, sizeof(map_size));
  outfs.close();
}

void AFLPassParent::writeMapSizeToFile(uint32_t map_size) {
  _writeSizeToFile(map_size, "AFL_BCCLANG_MAP_FILE");
}

void AFLPassParent::writeBBSizeToFile(uint32_t map_size) {
  _writeSizeToFile(map_size, "AFL_BCCLANG_BBMAP_FILE");
}

void AFLPassParent::writeBuildIDToFile(uint64_t buildID) {
  char* buildid_file = getenv("AFL_BCCLANG_BUILD_ID");
  if (!buildid_file) {
    FATAL("AFL_BCCLANG_BUILD_ID not defined");
  }

  std::ifstream idfile(buildid_file);
  if ( idfile.good() ) {
    FATAL("File %s already exists", buildid_file);
  }
  idfile.close();

  std::fstream idoutfs;
  idoutfs.open(buildid_file, std::fstream::out|std::fstream::binary);
  ASSERT ( idoutfs.is_open() );
  idoutfs.write((char*)&buildID, sizeof(buildID));
  idoutfs.close();
}

void AFLPassParent::writeDictToFile(utils::Dict2_t & dict, uint64_t buildID, BUILD_TYPE buildType, DICT_TYPE dictType) {
  
  bool isDictOptimized = (dictType == DICT_OPTIMIZED);
  bool isDictNormal = (dictType == DICT_NORMAL);
  bool isCoverageBuild = (buildType == BUILD_COVERAGE);
  bool isRunBuild = (buildType == BUILD_FUZZING);

  char* dict_file = getenv("AFL_BCCLANG_DICT_FILE");
  if (!dict_file) {
    FATAL("AFL_BCCLANG_DICT_FILE not defined");
  }

  if ( isCoverageBuild ) {
    /* Delete the file if it exists */
    if ( unlink(dict_file) < 0 and errno != ENOENT) {
      FATAL("Cannot delete %s: %s", dict_file, strerror(errno));
    }

  } else if ( isRunBuild && dict.size() ) {

    std::fstream outfs;
    outfs.open(dict_file, std::fstream::out|std::fstream::app);
    ASSERT ( outfs.is_open() );
    /* Tell AFL the kind of dictionary to run */
    outfs << "# AFL_DICT_TYPE=" << utils::getEnvVar("AFL_DICT_TYPE") 
          << "; AFL_COVERAGE_TYPE=" << utils::getEnvVar("AFL_COVERAGE_TYPE")
          << "; AFL_BUILD_ID=" << std::hex << std::setw(sizeof(buildID)*2) << std::setfill('0') << buildID << "\n";
    
    std::set<std::string> uniqueValues;
    size_t total = 0;
    for (auto & elt : dict) {
      std::string ss = elt.first;
      int add_to_file = (isDictOptimized || (isDictNormal && !uniqueValues.count(ss)) );
      total += !!add_to_file;
      if ( add_to_file ) {
        if ( isDictNormal ) { uniqueValues.insert(ss); }
        std::string debug = elt.second;
        /* filename_c_line="keyvalue" */
        ASSERT ( ss.size() >= 2 ); /* should have at least the first and last double-quote '"' */
        ASSERT ( ss[0] == '"' && ss[ss.size()-1] == '"' );
        if (ss.size()) {
          outfs << "AFL_" << (debug.size()?debug:"NDEBUG") << "=" << ss << "\n";
        }
      }
    }

    OKF("Created %zu entries in dictionary", total);
  }
}

void AFLPassParent::writeSrcToEdgeMappingToFile(CoverageInfo_t & coverageInfo) {
  char* edge2src_file = getenv("AFL_BCCLANG_COVERAGE_TO_SRC_FILE");
  if (!edge2src_file) {
    FATAL("AFL_BCCLANG_COVERAGE_TO_SRC_FILE not defined");
  }
  
  if ( coverageInfo.size() ) {

    std::ifstream efile(edge2src_file);
    if ( efile.good() ) {
      FATAL("File %s already exists", edge2src_file);
    }
    efile.close();

    std::fstream e2soutfs;
    e2soutfs.open(edge2src_file, std::fstream::out);
    ASSERT ( e2soutfs.is_open() );
    for (auto & elt : coverageInfo) {
      unsigned idx = elt.first;
      std::string srcInfo = elt.second;
      e2soutfs << idx << "=" << srcInfo << "\n";
    }
    e2soutfs.close();
  }
}

DICT_TYPE AFLPassParent::getDictType(void) { 
  if ( utils::isEnvVarSetTo("AFL_DICT_TYPE", "NORMAL") ) {
    return DICT_NORMAL;
  }

  if ( utils::isEnvVarSetTo("AFL_DICT_TYPE", "OPTIMIZED") ) {
    return DICT_OPTIMIZED;
  }
  
  FATAL("Invalid AFL_DICT_TYPE. Must be {NORMAL,OPTIMIZED}");
}

BUILD_TYPE AFLPassParent::getBuildType(void) {
  if ( utils::isEnvVarSetTo("AFL_BUILD_TYPE", "COVERAGE") ) {
    return BUILD_COVERAGE;
  }

  if ( utils::isEnvVarSetTo("AFL_BUILD_TYPE", "FUZZING") ) {
    return BUILD_FUZZING;
  }

  FATAL("Invalid AFL_BUILD_TYPE. Must be {COVERAGE,FUZZING}");
  
}

void AFLPassParent::_createAreaSizeFunction(llvm::Module& M, uint32_t Size, const char * fName) {
  LLVMContext &C = getGlobalContext();
  IntegerType * RetType = IntegerType::getInt32Ty(C); ASSERT (RetType);
  FunctionType * FT = FunctionType::get(RetType, false); ASSERT (FT);
  Constant * c = M.getOrInsertFunction(fName, FT); ASSERT (c);
  Function * F = cast<Function>(c);
  F->setCallingConv(CallingConv::C);
  BasicBlock * BB = BasicBlock::Create(getGlobalContext(), "entry", F);
  ConstantInt * CI = ConstantInt::get(RetType, Size);
  IRBuilder<> builder(BB);
  builder.CreateRet(CI);
}

void AFLPassParent::createAreaSizeFunction(Module &M, uint32_t Size) {
  _createAreaSizeFunction(M, Size, "__afl_get_area_size");
}


void AFLPassParent::createBBAreaSizeFunction(llvm::Module& M, uint32_t Size) {
  _createAreaSizeFunction(M, Size, "__afl_get_bbarea_size");
}

bool AFLPassParent::isDictRecordedToBB(BasicBlock & BB) {
  if ( utils::isDictRecordedToBB(BB, C2U_DICT) ) {
    return true;
  }
  if ( utils::isDictRecordedToBB(BB, S2U_DICT) ) {
    return true;
  }
  if ( utils::isDictRecordedToBB(BB, utils::origin_to_multiple(S2U_DICT)) ) {
    return true;
  }
  if ( utils::isDictRecordedToBB(BB, utils::origin_to_multiple(SIC_DICT)) ) {
    return true;
  }
  return false;
}

void AFLPassParent::recordToDict(utils::DictElt2 elmt, utils::Dict2_t & dict, unsigned id) {

  utils::PairElt_t elmt2(elmt.first, elmt.second + "_" + utils::to_string<unsigned>(id, std::hex)); /* add id information */
  if ( dict.count(elmt2) ) {
    /* For debugging */
    //errs() << "first:" << elmt2.first << "\n";
    //errs() << "second:" << elmt2.second << "\n";
  } else {
    /* Note: it is possible to have a duplicate if codelooks like this:
    
      if (x==123 && y==123)

      same line and same value. For us it's fine to discard the previous one in this case
      since hiting the second branch requires hitting the first one. So for optimized dictionary
      based on seed we can still select the approriate dict words

     */
    //ASSERT ( !dict.count(elmt2) );
    dict.insert(elmt2);
  }
}

void AFLPassParent::recordDictToEdgeMapping(BasicBlock & BB, utils::Dict2_t & dict, unsigned id) {
  bool found = false;

  for ( auto & I : BB ) {
    size_t n = 0;

    /* Warning: S2U supports both single and multiple. That's a bit ugly... it's the result of additional
      code added for grabing constant struct. TODO: harmonize it */
    utils::DictElt2 elmt = utils::getDict2RecordFromInstr(getGlobalContext(), I, S2U_DICT, found);
    if ( found ) {
      //errs() << elmt.first << "\n";
      recordToDict(std::make_pair(elmt.first, "S2U_" + elmt.second), dict, id);
    } 
    
    elmt = utils::getDict2RecordFromInstr(getGlobalContext(), I, utils::origin_to_multiple(S2U_DICT, n), found);
    while(found) {
      recordToDict(std::make_pair(elmt.first, utils::origin_to_multiple("S2U", n) + "_" + elmt.second), dict, id);
      ++n;
      elmt = utils::getDict2RecordFromInstr(getGlobalContext(), I, utils::origin_to_multiple(S2U_DICT, n), found);
    };

    /* C2U : single only*/
    elmt = utils::getDict2RecordFromInstr(getGlobalContext(), I, C2U_DICT, found);
    if ( found ) {
      //errs() << elmt.first << "\n";
      //errs() << "I=" <<  I << "\n";
      recordToDict(std::make_pair(elmt.first, "C2U_" + elmt.second), dict, id);
    }

    /* SIC: multiple only */
    n = 0;
    elmt = utils::getDict2RecordFromInstr(getGlobalContext(), I, utils::origin_to_multiple(SIC_DICT, n), found);
    while(found) {
      //errs() << elmt.first << "\n";
      recordToDict(std::make_pair(elmt.first, utils::origin_to_multiple("SIC", n) + "_" + elmt.second), dict, id);
      ++n;
      elmt = utils::getDict2RecordFromInstr(getGlobalContext(), I, utils::origin_to_multiple(SIC_DICT, n), found);
    };
  }
}