#include "../config.h"
#include "../debug.h"
#include "common.h"

#include <iostream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iostream>
#include <string.h>
// for demangle
#include <cxxabi.h>

#include "llvm/IR/DebugInfo.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Constants.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/IR/CallSite.h"

using namespace llvm;

namespace utils {


void remove_char(std::string & s, char c) {
  std::string::iterator end_pos = std::remove(s.begin(), s.end(), c);
  s.erase(end_pos, s.end());
}

std::string Tolower(std::string s) {
  std::string ret;
  for (size_t i=0; i<s.size(); ++i) {
    ret.push_back(std::tolower(s[i]));
  }
  return ret;
}

const char * getEnvVar(const char * s) {
  char * env = getenv(s);
  return env ? env : "";
}

bool isEnvVarSet(const char * s) {
  const char * env = getEnvVar(s);
  return ( *env != '\0' && strcmp(env, "0") != 0 );
}

bool isEnvVarSetTo(const char * s, const char * v) {
  const char * env = getEnvVar(s);
  return ( strcmp(env, v) == 0 );
}



void split(const std::string& s, char delim, std::set<std::string>& v, bool demangle) {
  std::stringstream ss(s);
  std::string item;
  while (getline(ss, item, delim)) {
    if (demangle) {
      item = utils::demangleName(item);
    }
    if (!v.count(item))
        v.insert(item);
  }
}

void split(const std::string& s, char delim, std::vector<std::string>& v, bool demangle) {
  std::stringstream ss(s);
  std::string item;
  while (getline(ss, item, delim)) {
    if (demangle) {
      item = utils::demangleName(item);
    }
    if (std::find(v.begin(), v.end(), item) == v.end())
      v.push_back(item);
  }
}

std::string getDebugInfo(Instruction &I) {
  std::string debugInfo;
  BasicBlock & BB = *I.getParent();
  MDNode* dbg = I.getMetadata("dbg");

  /* If we cannot find dbg on the instruction, try in the entire parent block */
  if ( !dbg ) {
    for (auto & II : BB) {
      if ( (dbg = II.getMetadata("dbg")) ) {
        break;
      }
    }
  }
  
  if ( dbg ) {
    DebugLoc DLoc(dbg);
    DILocation * DILoc = DLoc.get();
    char * dup = strdup(DILoc->getFilename().str().c_str());
    char * name = basename(dup);
    std::string filename = std::string(name);
    free (dup);
    std::string line = std::to_string(DILoc->getLine());
    std::string point = ".";
    std::string rep = "_";
    std::string minus = "-";
    while ( filename.find(minus) != std::string::npos ) {
      filename = filename.replace(filename.find(minus),minus.length(),(rep+rep).c_str(), 2);
    }
    while ( filename.find(point) != std::string::npos ) {
      filename = filename.replace(filename.find(point),point.length(),rep) + rep + line;
    }

    debugInfo = filename;
  }

  return debugInfo;
}

bool starts_with(const std::string& str, const std::string& prefix) {
  if(prefix.length() > str.length()) { return false; }
  return str.substr(0, prefix.length()) == prefix;
}

bool is_llvm_dbg_intrinsic(Instruction & instr) {
  const bool is_call = instr.getOpcode() == Instruction::Invoke ||
                       instr.getOpcode() == Instruction::Call;
  if(!is_call) { return false; }

  CallSite cs(&instr);
  Function* calledFunc = cs.getCalledFunction();

  if (calledFunc != NULL) {
    const bool ret = calledFunc->isIntrinsic() &&
                     starts_with(calledFunc->getName().str(), "llvm.");
    llvm::errs() << instr << ":" << ret << "\n";
    return ret;
  } else { 
    return false;
  }
}

static bool is_afl_function_call(Instruction & II) {
  const bool is_call = II.getOpcode() == Instruction::Call;
  if(!is_call) { return false; }

  CallSite cs(&II);
  Function* calledFunc = cs.getCalledFunction();

  if (calledFunc != NULL) {
    return calledFunc->getName().equals("__afl_bb_trace");
  } 

  return false;
}

std::string addSrcInfo(BasicBlock &BB, std::string sinit) {

  std::string srcInfo;
  std::set<std::string> Set;
  for (auto & II : BB) {

    /* Skip intrinsics. Why? Because code like
        int a;
      will have an intrinsic associated with it, and we don't want to
      account for it thru functional coverage -- see paper
     */
    if ( is_llvm_dbg_intrinsic(II) ) { continue; }

    if ( is_afl_function_call(II) ) { continue; }

    if ( MDNode* dbg = II.getMetadata("dbg") ) {
      DebugLoc DLoc(dbg);
      DILocation * DILoc = DLoc.get();
      Set.insert( DILoc->getFilename().str() + ":" + std::to_string(DILoc->getLine()) );
    }
  }

  for (auto s : Set) {
    srcInfo += s + ",";
  }

  return ((sinit.size()) ? (sinit + ",") : "") + srcInfo.substr(0, srcInfo.size() > 0 ? srcInfo.size()-1: 0);
}

/* Replace original instruction and copy the dictionnary information too */
void ReplaceInstWithInst (LLVMContext & C, std::string origin, BasicBlock::InstListType & BIL, BasicBlock::iterator & OldI, Instruction & NewI) {
  bool found = false;
  DictElt elt  = getDictRecordFromInstr(C, *OldI, origin, found);
  ReplaceInstWithInst(BIL, OldI, &NewI);
  if ( found ) {
    recordDictToInstr(C, NewI, elt, origin, false /* already stringified */);
  }
}

bool CopyDictToInst(LLVMContext & C, std::string origin, Instruction & FromI, Instruction & ToI) {
  bool found = false;
  DictElt elt  = getDictRecordFromInstr(C, FromI, origin, found);
  if ( found ) {
    recordDictToInstr(C, ToI, elt, origin, false /* already stringified */);
  }
  return found;
}

void recordDictToInstr(LLVMContext & C, Instruction & I, utils::DictElt & elt, std::string origin, bool doStringify, bool isText) {

  // http://jiten-thakkar.com/posts/how-to-read-and-write-metadata-in-llvm
  std::string ss = doStringify ? utils::Stringify(elt.getFirst(), isText) : elt.getFirst();
  std::string debug = elt.getSecond();
  std::string value;
  if (ss.size()) {
    /* filename_c_line=keyvalue */
    if ( !doStringify ) {
      ASSERT ( ss.size() >= 2 ); /* should have at least the first and last double-quote '"' */
      ss = ss.substr(1, ss.size() - 2);
    }
    value = (debug.size()?debug:"NDEBUG") + "=\"" + ss + "\"";
    MDString * S = MDString::get(C, value); ASSERT (S);
    MDNode* Meta = MDNode::get(C, S);
    /* origin = value */
    I.setMetadata(origin, Meta);

  }
        
}

void init_metanames(void) {
  /* Creating the custon kinds seems necessary. Without this I encountered problems when adding metadata */
  getGlobalContext().getMDKindID(S2U_DICT); /* create the S2U_DICT meta kind */
  getGlobalContext().getMDKindID(C2U_DICT); /* create the C2U_DICT meta kind */
  /* Initialiaze the meta names */
  for ( size_t n=0; n< utils::max_origin_multiple(); ++n ) {
    getGlobalContext().getMDKindID(utils::origin_to_multiple(S2U_DICT, n)); /* create the S2U_DICT meta kind */
    getGlobalContext().getMDKindID(utils::origin_to_multiple(SIC_DICT, n)); /* create the SIC_DICT meta kind */
  }
  getGlobalContext().getMDKindID("FuncPointedToList"); /* For retrieveing DSA's generated list of function. See dsa/lib/DSA/CallTargets.cpp */
}

std::string origin_to_multiple(std::string origin, size_t n) {
  return origin + "_" + std::to_string(n);
}

/* use when we need an instruction to have mtultiple meta from same origin */
void recordMultipleDictToInstr(LLVMContext & C, Instruction & I, DictElt & elt, std::string origin, bool doStringify, bool isText) {
  /* Find the available origin as origin-n */
  size_t n = 0;
  DictElt elmt("","");
  std::string orig;
  while (1) {
    bool found = false;
    orig = origin_to_multiple(origin, n);
    elmt = getDictRecordFromInstr(C, I, orig, found);
    if (!found) {
      break;
    } else {
      ++n;
    }
  }

  ASSERT ( n < max_origin_multiple() );
  recordDictToInstr(C, I, elt, orig, doStringify, isText);
}


DictElt2 getDict2RecordFromInstr(LLVMContext & C, Instruction & I, std::string origin, bool & found) {
  DictElt elmt = getDictRecordFromInstr(C, I, origin, found);
  return std::make_pair(elmt.getFirst(), elmt.getSecond());
}

DictElt getDictRecordFromInstr(LLVMContext & C, Instruction & I, std::string origin, bool & found, bool remove) {
  
  if ( MDNode* Meta = I.getMetadata(origin) ) {
    
    const std::string s = cast<MDString>(Meta->getOperand(0))->getString().str();
    std::vector<std::string> values;
    split(s, '=', values);
    ASSERT ( values.size() == 2 );
    found = true;
    std::string ss = values.at(1);
    std::string debug = values.at(0);
    if (remove) I.setMetadata(origin, NULL);
    return DictElt(ss, debug);
  }
  
  found = false;
  return DictElt("", "");
}


bool isDictRecordedToBB(llvm::BasicBlock & BB, std::string origin) {
  for ( auto & I : BB ) {
    if ( isDictRecordedToInstr(I, origin) ) {
      return true;
    }
  }
  return false;
}

bool isDictRecordedToInstr(Instruction & I, std::string origin) {
  return (I.getMetadata(origin) != NULL);
}

std::string getSrcInfo(BasicBlock &BB) {
  return addSrcInfo(BB, "");
}

std::string Stringify(const std::string & data, bool isText) {

  std::string TokenStr;
  size_t len = data.size();

  if (len < MIN_AUTO_EXTRA || len > MAX_AUTO_EXTRA) {
    WARNF("Ignoring token of invalid length '%s'", data.c_str());
    return "";
  }

  for(size_t i = 0; i < len; ++i) {

    /* Do not append the last '\0' if it's text */
    if (isText && i==len-1 && data[len-1] == '\0') {
      return TokenStr;
    }

    uint8_t c = data[i];

    switch (c) {
      case 0 ... 31:
      case 127 ... 255:
      /* = and " are special characters as we keep meta as name="value" */
      case '=': 
      case '\"':
      case '\\': {

        char buf[5] { };
        sprintf(buf, "\\x%02x", c);
        TokenStr.append(buf);
        break;
      }

      default: {
        if (isText) {
          TokenStr.push_back(c);
        } else {
          char buf[5] { };
          sprintf(buf, "\\x%02x", c);
          TokenStr.append(buf);
        } 
        break;
      }
    }
  }

  return TokenStr;
}


// http://aviral.lab.asu.edu/llvm-def-use-use-def-chains/
// https://stackoverflow.com/questions/35370195/llvm-difference-between-uses-and-user-in-instruction-or-value-classes
void getUsersOf(Value & V, std::vector<Instruction*> & Userlist) {
  for(auto U : V.users()){  // U is of type User*
    if (auto I = dyn_cast<Instruction>(U)){
      // an instruction uses V
      Userlist.push_back(I);
    }
  }
}


static std::string demangle(StringRef SRef) {
  int status = -1; 
  const char* name = SRef.str().c_str();
  std::unique_ptr<char, void(*)(void*)> res { abi::__cxa_demangle(name, NULL, NULL, &status), std::free };
  return (status == 0) ? res.get() : std::string(name);
}
  
std::string demangleName( StringRef SRef ) {
  std::string demangledF = demangle(SRef);
  std::size_t pos = demangledF.find("(");
  if ( std::string::npos == pos  ) { return demangledF; }
  return demangledF.substr (0, pos); 
}

} // end namespace