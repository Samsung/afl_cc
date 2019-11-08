#include "clang/AST/AST.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Rewrite/Core/Rewriter.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Lex/HeaderSearchOptions.h"
#include "clang/Lex/Lexer.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Lex/TokenConcatenation.h"
#include "clang/Tooling/CompilationDatabasePluginRegistry.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/LineIterator.h"


using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

/* defined in each tool's .cpp */
extern cl::OptionCategory MyToolCategory;


static cl::opt<std::string> MyCompilationDbFile("cdb", 
											cl::desc("Compilation dtabase file"), 
											cl::value_desc("filename"), 
											cl::Optional,
											cl::cat(MyToolCategory));


/* relevant files:
	clang/lib/Tooling/CompilationDatabase.cpp
	clang/lib/Tooling/Tooling.cpp
	clang/lib/Tooling/JSONCompilationDatabase.cpp
	clang/lib/Tooling/CommonOptionsParser.cpp

	clang/include/clang/Tooling/CompilationDatabase.h
	clang/include/clang/Tooling/Tooling.h
   
   	As our projects do not always use cmake, I use a text file with the args

   	ClangTool::run() calls getCompileCommands() only, so we need not override other functions
   	of CompilationDatabase
*/


class MyCompilationDatabase: public CompilationDatabase {
	public:
		std::vector<std::string> getAllFiles() const {
			return std::vector<std::string>();
		}

		std::vector<CompileCommand> getAllCompileCommands() const {
			return std::vector<CompileCommand>();
		}

		MyCompilationDatabase(Twine Directory, ArrayRef<std::string> CommandLine) {
			CompileCommands.clear();
			CompileCommands.push_back(CompileCommand(Directory, StringRef(), CommandLine));
		}


		std::vector<CompileCommand> getCompileCommands(StringRef FilePath) const {
			std::vector<CompileCommand> Result(CompileCommands);
			Result[0].CommandLine.push_back(FilePath);
			Result[0].Filename = FilePath;
			return Result;
		}

		static std::unique_ptr<MyCompilationDatabase> loadFromFile(StringRef Path, std::string &ErrorMsg) {
			ErrorMsg.clear();
			llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> File = llvm::MemoryBuffer::getFile(Path);
			if (std::error_code Result = File.getError()) {
				ErrorMsg = "Error while opening fixed database: " + Result.message();
				return nullptr;
			}
			std::vector<std::string> Args{llvm::line_iterator(**File), llvm::line_iterator()};
			return llvm::make_unique<MyCompilationDatabase>(llvm::sys::path::parent_path(Path), std::move(Args));
		}

	private:
		std::vector<CompileCommand> CompileCommands;
};



namespace {

class MyCompilationDatabasePlugin : public CompilationDatabasePlugin {
	std::unique_ptr<CompilationDatabase>
	loadFromDirectory(StringRef Directory, std::string &ErrorMessage) override {
		/*
			conpile_flags.txt must contain:
				/path/to/compiler\n
				flag1\n
				flag2\n
			No source code or output file
			Example:
				/path/to/clang++
				-DMYMACRO=3
				-I/path/to/inc
		*/
		//SmallString<1024> DatabasePath(Directory);
		//llvm::sys::path::append(DatabasePath, MyCompilationDbFile);
		return MyCompilationDatabase::loadFromFile(MyCompilationDbFile, ErrorMessage);
	}
};

} // namespace

static CompilationDatabasePluginRegistry::Add<MyCompilationDatabasePlugin>
WHATEVER("my-compilation-database", "Reads plain-text flags file (compile_flags.txt)");



