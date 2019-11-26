SOURCES		= normalize_rewriter.cpp CompilationDb.cpp
EXECUTABLES	= normalize_rewriter

OBJECTS		= $(SOURCES:.cpp=.o)


ifeq ($(strip $(LLVM_CONFIG)),)
$(error LLVM_CONFIG is not set)
endif

# LLVM 
SRC_LLVM_DIR	:= $(shell $(LLVM_CONFIG) --src-root)
LLVM_CXXFLAGS 	:= $(shell $(LLVM_CONFIG) --cxxflags) #-fno-rtti
LLVM_LDFLAGS 	:= $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBS 		:= $(shell $(LLVM_CONFIG) --libs --system-libs)
LLVM_INC_DIR	:= $(shell $(LLVM_CONFIG) --includedir)
LLVM_LIB_DIR 	:= $(shell $(LLVM_CONFIG) --libdir)
LLVM_BIN_DIR 	:= $(shell $(LLVM_CONFIG) --bindir)
LLVM_SRC_DIR 	:= $(shell $(LLVM_CONFIG) --src-root)
LLVM_BUILD_DIR 	:= $(LLVM_SRC_DIR)/build

# Clang. Note: tools is hardcoded
CLANG_INCS		:= -I$(LLVM_SRC_DIR)/tools/clang/include/ \
				   -I$(LLVM_BUILD_DIR)/tools/clang/include/


CXX				:= $(LLVM_BIN_DIR)/clang++
CXXFLAGS 		:= $(LLVM_CXXFLAGS) -I$(LLVM_INC_DIR) $(CLANG_INCS) -Wall -Werror -Wno-error=unknown-warning-option #-Wextra
LDFLAGS 		:= $(LLVM_LDFLAGS) $(LLVM_LIBS)

# is there a way to get these programmatically?
CLANG_LIBS = -Wl,--start-group \
			-lclangTooling\
			-lclangFrontendTool\
			-lclangFrontend\
			-lclangDriver\
			-lclangSerialization\
			-lclangCodeGen\
			-lclangParse\
			-lclangSema\
			-lclangStaticAnalyzerFrontend\
			-lclangStaticAnalyzerCheckers\
			-lclangStaticAnalyzerCore\
			-lclangAnalysis\
			-lclangARCMigrate\
			-lclangRewrite\
			-lclangRewriteFrontend\
			-lclangEdit\
			-lclangAST\
			-lclangLex\
			-lclangBasic\
			-Wl,--end-group

all: $(EXECUTABLES)

normalize_rewriter: normalize_rewriter.o CompilationDb.o
	$(CXX) $(CXXFLAGS) -o $@ $^ $(CLANG_LIBS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) -c $(CXXFLAGS) $< -o $@

clean:
	-rm -f $(EXECUTABLE) $(OBJECTS) *~
