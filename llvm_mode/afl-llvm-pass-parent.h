#pragma once

#include "utils.h"

#include "llvm/IR/Module.h"

typedef enum {
	DICT_NORMAL = 0,
	DICT_OPTIMIZED
} DICT_TYPE;

typedef enum {
	BUILD_FUZZING = 0,
	BUILD_COVERAGE
} BUILD_TYPE;


class AFLPassParent {

	public:

		AFLPassParent();
		virtual ~AFLPassParent() {}

  	protected:
  		typedef std::set< std::pair<unsigned, std::string> > CoverageInfo_t;
  		
		void createAreaSizeFunction(llvm::Module& M, uint32_t Size);
		void createBBAreaSizeFunction(llvm::Module& M, uint32_t Size);
		bool isDictRecordedToBB(llvm::BasicBlock & BB);
		void recordToDict(utils::DictElt2 elmt, utils::Dict2_t & dict, unsigned id);
		void recordDictToEdgeMapping(llvm::BasicBlock & BB, utils::Dict2_t & dict, unsigned id);
		uint64_t generateBuildID(void);
		void writeMapSizeToFile(uint32_t size);
		void writeBBSizeToFile(uint32_t size);
		void writeBuildIDToFile(uint64_t buildID);
		void writeDictToFile(utils::Dict2_t & dict, uint64_t buildID, BUILD_TYPE buildType, DICT_TYPE dictType);
		void writeSrcToEdgeMappingToFile(CoverageInfo_t & coverageInfo);
		DICT_TYPE getDictType(void);
		BUILD_TYPE getBuildType(void);

	private:
		void _writeSizeToFile(uint32_t size, const char * env);
		void _createAreaSizeFunction(llvm::Module& M, uint32_t Size, const char * fName);
};
