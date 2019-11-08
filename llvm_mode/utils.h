#pragma once

#include <string>
#include <set>

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <iomanip>

#include "llvm/Pass.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/IR/Instruction.h"



/* Use our own assert() function - depending on options, compiler will make it a noop */
#ifdef ASSERT
#undef ASSERT
#endif
#define ASSERT(x) if (!(x)) {errs() << "assert( " << #x << " ) failed in file " << __BASE_FILENAME__ << " at line " << __LINE__ << "\n"; exit(-1); }

// Make sure calls to assert() are not removed
// #ifdef NDEBUG
// # undef NDEBUG
// #endif
// #ifdef _NDEBUG
// # undef _NDEBUG
// #endif

namespace utils {

	/* Structure for dictionary. Custom operators to remove duplicates having the same value */
	typedef std::pair< std::string,std::string > PairElt_t;
	typedef struct DictElt {
	  PairElt_t pair;
	  bool operator<( const DictElt& other ) const {
	    return this->pair.first.compare(other.pair.first) < 0;
	  }
	  bool operator==( const DictElt& other ) const {
	    return (this->pair.first.compare(other.pair.first) == 0);
	  }
	  std::string getFirst(void) const { return this->pair.first; }
	  std::string getSecond(void) const { return this->pair.second; }
	  DictElt(std::string s1, std::string s2) { this->pair = std::make_pair(s1, s2); }
	  DictElt(const DictElt &p2) { this->pair = p2.pair;  }
	} DictElt;
	typedef std::set< DictElt > Dict_t;
	/* Both elements must be equal for the element to be equal */
	typedef PairElt_t DictElt2;
	typedef std::set< DictElt2 > Dict2_t;

	/* to_string in hex */
	template <class T>
	std::string to_string(T t, std::ios_base & (*f)(std::ios_base&)) {
	  std::ostringstream oss;
	  oss << f << std::setw(8) << std::setfill('0') << std::hex << t;
	  return oss.str();
	}

	inline size_t max_origin_multiple(void) { return 20; }
	extern void init_metanames(void);
	extern bool is_llvm_dbg_intrinsic(llvm::Instruction & instr);
	extern bool starts_with(const std::string& str, const std::string& prefix);
	extern std::string origin_to_multiple(std::string origin, size_t n = 0);
	extern void ReplaceInstWithInst (llvm::LLVMContext & C, std::string origin, llvm::BasicBlock::InstListType & BIL, llvm::BasicBlock::iterator & OldI, llvm::Instruction & NewI);
	extern bool CopyDictToInst(llvm::LLVMContext & C, std::string origin, llvm::Instruction & FromI, llvm::Instruction & ToI);
	extern utils::DictElt getDictRecordFromInstr(llvm::LLVMContext & C, llvm::Instruction & I, std::string origin, bool & found, bool remove = false);
	extern utils::DictElt2 getDict2RecordFromInstr(llvm::LLVMContext & C, llvm::Instruction & I, std::string origin, bool & found);
	extern void recordDictToInstr(llvm::LLVMContext & C, llvm::Instruction & I, utils::DictElt & elt, std::string origin, bool doStringify = true, bool isText = false);
	extern void recordMultipleDictToInstr(llvm::LLVMContext & C, llvm::Instruction & I, utils::DictElt & elt, std::string origin, bool doStringify = true, bool isText = false);
	extern bool isDictRecordedToInstr(llvm::Instruction & I, std::string origin);
	extern bool isDictRecordedToBB(llvm::BasicBlock & BB, std::string origin);
	extern void split(const std::string& s, char delim, std::set<std::string>& v, bool demangle = false);
	extern void split(const std::string& s, char delim, std::vector<std::string>& v, bool demangle = false);
	extern std::string getDebugInfo(llvm::Instruction &I);
	extern std::string getSrcInfo(llvm::BasicBlock &BB);
	extern std::string addSrcInfo(llvm::BasicBlock &BB, std::string s);
	extern std::string Tolower(std::string s);
	extern std::string Stringify(const std::string & data, bool isText = true);
	extern void getUsersOf(llvm::Value & V, std::vector<llvm::Instruction*> & Userlist);
	extern bool isEnvVarSet(const char * s);
	extern bool isEnvVarSetTo(const char * s, const char * v);
	extern const char * getEnvVar(const char * s);
	extern std::string demangleName(llvm::StringRef SRef);
}