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
#include "clang/AST/Stmt.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/LineIterator.h"


#include <sstream>
#include <string>

#include "common.h"

#define TOOL_NAME "Normalize"

using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;

/*
	Inspired by https://github.com/loarabia/Clang-tutorial/blob/master/CIrewriter.cpp

*/


// https://clang.llvm.org/doxygen/classclang_1_1tooling_1_1CommonOptionsParser.html
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
cl::OptionCategory MyToolCategory(TOOL_NAME " rewriter options");

class Utils {
	public:
		
		static unsigned get_line(const SourceManager & SM, SourceLocation SL) {
			ASSERT (SL.isValid());
			if(!SL.isFileID()) {
				SL = SM.getExpansionLoc(SL); ASSERT (SL.isValid());
				ASSERT(SL.isFileID());
			}

			PresumedLoc PLoc = SM.getPresumedLoc(SL); ASSERT(PLoc.isValid());
			//std::string filename = PLoc.getFilename();
			unsigned lineNo = PLoc.getLine();
			//unsigned colNo = PLoc.getColumn();
			return lineNo;
		}

		static unsigned get_column(const SourceManager & SM, SourceLocation SL) {
			ASSERT (SL.isValid());
			if(!SL.isFileID()) {
				SL = SM.getExpansionLoc(SL); ASSERT (SL.isValid());
				ASSERT(SL.isFileID());
			}

			PresumedLoc PLoc = SM.getPresumedLoc(SL); ASSERT(PLoc.isValid());
			//std::string filename = PLoc.getFilename();
			unsigned colNo = PLoc.getColumn();
			return colNo;
		}
};

/* By implementing RecursiveASTVisitor, we can specify which AST nodes
   we're interested in by overriding relevant methods.
*/
class MyASTVisitor : public RecursiveASTVisitor<MyASTVisitor> {
	public:
		MyASTVisitor(Rewriter &RW) : _RW(RW) {}

	/*	a ? b : c 
			=>
		a ?
		b :
		c
	*/
	void InstrumentSelect(ConditionalOperator * co) { 
		
		ASSERT (!isa<CompoundStmt>(co));
		
		SourceManager & SM = _RW.getSourceMgr();

		Expr * Cond = co->getCond(); ASSERT (Cond);
		Expr * True = co->getTrueExpr(); ASSERT (True);
		Expr * False = co->getFalseExpr(); ASSERT (False);

		unsigned CondLine = Utils::get_line(SM, Cond->getLocStart());
		unsigned TrueLine = Utils::get_line(SM, True->getLocStart());
		unsigned FalseLine = Utils::get_line(SM, False->getLocStart());

		if ( CondLine == TrueLine ) {
			_RW.InsertText(True->getLocStart(), "\n" , true, false);
		}

		if ( TrueLine == FalseLine ) {
			_RW.InsertText(False->getLocStart(), "\n", true, false);
		}
	}

	/*	a BinOp b
			=>
		a BinOp
		b

		where BinOp is || or &&

		Note: clang-format has an option BreakBeforeBinaryOperators, but this will add
		a line for all binary operators. We only want && and ||
	 */
	void InstrumentBinaryOperator1(BinaryOperator * bo) {
		ASSERT (!isa<CompoundStmt>(bo));
		BinaryOperatorKind code = bo->getOpcode();
		
		if ( code == BinaryOperatorKind::BO_LAnd || code == BinaryOperatorKind::BO_LOr ) {
			SourceManager & SM = _RW.getSourceMgr();
			Expr * Right = bo->getRHS(); ASSERT(Right);
			Expr * Left = bo->getLHS(); ASSERT(Left);
			unsigned LeftLine = Utils::get_line(SM, Left->getLocStart());
			unsigned RightLine = Utils::get_line(SM, Right->getLocStart());
			if ( LeftLine == RightLine ) {
				_RW.InsertText(Right->getLocStart(), "\n", true, true);
			}			
		}
	}
	/*
		a = b = c;

			=>

		a =
		b =
		c;

		Dsiabled for now. I found this does not play nice with src code mapping...
	*/
	void InstrumentBinaryOperator2(BinaryOperator * bo) {
		ASSERT (!isa<CompoundStmt>(bo));
		BinaryOperatorKind code = bo->getOpcode();
		SourceManager & SM = _RW.getSourceMgr();

		if ( code == BinaryOperatorKind::BO_Assign ) {
			
			Expr * Right = bo->getRHS(); ASSERT(Right);
			Expr * Left = bo->getLHS(); ASSERT(Left);

			Expr * SubRight = Right;
			//errs() << "ExprO:\n"; Right->dump();
			while (isa<CastExpr>(SubRight)) {
				SubRight = cast<CastExpr>(SubRight)->getSubExpr();
				//errs() << "Expr:\n"; Right->dump();
			}
			
			/* Only instrument if there are more than two elements */
			if (isa<BinaryOperator>(SubRight) && cast<BinaryOperator>(SubRight)->getOpcode() == BinaryOperatorKind::BO_Assign) {
				unsigned RightLine = Utils::get_line(SM, Right->getLocStart());
				unsigned LeftLine = Utils::get_line(SM, Left->getLocStart());
				if ( RightLine == LeftLine ) {
					_RW.InsertText(Right->getLocStart(), "\n", true, true);
				} 
			}
		}
	}

	/*
		for (i;c;inc)

			=>

		for (i;
			 c;
			 inc)

		This way it is identical as
		i;
		for (c)
			...
			inc
	*/
	void InstrumentForStatement(ForStmt * s) {
		SourceManager & SM = _RW.getSourceMgr();

		Expr * Cond = s->getCond();
 		Expr * Inc = s->getInc(); 
 		Stmt * Init = s->getInit();

 		if (Init && Cond) {
 			unsigned InitLine = Utils::get_line(SM, Init->getLocStart());
	 		unsigned CondLine = Utils::get_line(SM, Cond->getLocStart());

	 		if (InitLine == CondLine) {
	 			_RW.InsertText(Cond->getLocStart(), "\n", true, true);
	 		}
 		}

 		if (Cond && Inc) {

	 		unsigned CondLine = Utils::get_line(SM, Cond->getLocStart());
	 		unsigned IncLine = Utils::get_line(SM, Inc->getLocStart());

	 		if (CondLine == IncLine) {
	 			_RW.InsertText(Inc->getLocStart(), "\n", true, true);
	 		}
	 	}
	} 

			
	// Override Statements which includes expressions and more
	// TODO: put in different function, eg VisitBinaryOperator, etc
	bool VisitStmt(Stmt *s) {

		SourceManager & SM = _RW.getSourceMgr();
		if (SM.isInSystemHeader(s->getLocStart())) {
			return true;
		}

		if (isa<ConditionalOperator>(s)) {
			/* WARNING: there were problems when calling InstrumentString() after InstrumentSelect() 
				example: c ? "a" : "b"

				A simple, but ugly fix, is to delay InstrumentSelect() till the end
				So I just keep them in a set, and instrument them in Finalize()

				Note: I no longer do the InstrumentString() -- see comment
			*/
			InstrumentSelect(cast<ConditionalOperator>(s));
			//COSet.insert(cast<ConditionalOperator>(s));
		} else if (isa<BinaryOperator>(s)) {
			InstrumentBinaryOperator1(cast<BinaryOperator>(s));
			//InstrumentBinaryOperator2(cast<BinaryOperator>(s));
		} else if (isa<DeclStmt>(s)) {
			InstrumentVariableDeclarations(cast<DeclStmt>(s));
		} else if (isa<ForStmt>(s)) {
			InstrumentForStatement(cast<ForStmt>(s));
		} else if (isa<StringLiteral>(s)) {
			InstrumentString(cast<StringLiteral>(s));
		} else {
			// errs() << "new:\n";
			// s->dump();
		}

		return true; // returning false aborts the traversal
	}

	// inspired by https://stackoverflow.com/questions/2417588/escaping-a-c-string
	std::string escapeString(std::string s) {
		std::string escapedText = "";
		for(char c : s) {

			if ( c == '"') {
				escapedText += '\\';
				escapedText += c;
			} else if((c >= 32) && (c <= 126) && (c != '\\') && 
					/* I also escape these, because
						\nab -> \0aab and the compiler thinks I'm trying to declare a 2 byte integer using hex representation :(
					 */
					!((c >= '0' && c<= '9') || (c >= 'a' && c<= 'f') || (c >= 'A' && c<= 'F'))) {
		        escapedText += c;
		    } else {
		    
		        std::stringstream stream;
		        // if the character is not printable
		        // we'll convert it to a hex string using a stringstream
		        // note that since char is signed we have to cast it to unsigned first
		        stream << std::hex << (unsigned int)(unsigned char)(c);
		        std::string code = stream.str();
		        escapedText += std::string("\\x")+(code.size()<2?"0":"")+code;
		        // alternatively for URL encodings:
		        //s += std::string("%")+(code.size()<2?"0":"")+code;
			}
		}

		return escapedText;
	}

	/*
		"hello"
		"world"

			=>

		"helloworld"

		Note: I no longer do this. because:
		1. it's risky to manipulate strings
		2. the LLVM IR does not care about multi-line strings. The line it uses is the one where the first string is
	*/
	void InstrumentString(StringLiteral *s) { 
		SourceRange SR = s->getSourceRange();
		std::string Text = _RW.getRewrittenText(SR);
		std::string oneLineText = s->getString();
		// errs() << "oneLineText:'" << oneLineText << "'\n";
		// errs() << "Text:'" << Text << "'\n";
		// errs() << "New:'" << "\"" + escapeString(oneLineText) + "\"'\n";
		//_RW.ReplaceText(SR, "\"" + escapeString(oneLineText) + "\"");
	}

	// Done by clang-format.
	// void InstrumentListDeclarations(DeclStmt * DS) {
	// 	errs() << "InstrumentListDeclarations:"; //DS->dump();
	// 	if (Decl * D = DS->getSingleDecl()) {
	// 		if (VarDecl * VD = dyn_cast<VarDecl>(D)) {
	// 			Expr * Init = VD->getInit();
	// 			InitListExpr * ILE = dyn_cast<InitListExpr>(Init);
	// 			if (Init && ILE) {
	// 				ILE->dump();
	// 				SourceRange SR = D->getSourceRange();
	// 				std::string Text = _RW.getRewrittenText(SR);
	// 				errs() << "Text:" << Text << "\n";
	//				Text.erase(std::remove(Text.begin(), Text.end(), '\n'), Text.end());
	//				_RW.ReplaceText(SR, Text);
	// 			}
	// 		}
	// 	}
	// }
	
	/*
		int a, b, c;

			=>

		int a,
			b,
			c;
	*/
	void InstrumentVariableDeclarations(DeclStmt * DS) {
		
		if (!DS->isSingleDecl()) {
			//DS->dump();
			SourceManager & SM = _RW.getSourceMgr();
			std::set<unsigned> Lines;

			for (auto & D : DS->decls()) {
				
				if (VarDecl * VD = dyn_cast<VarDecl>(D)) {
					//VD->dump();
					unsigned startLine = Utils::get_line(SM, VD->getLocStart());
					unsigned endLine = Utils::get_line(SM, VD->getLocEnd());
					
					//ASSERT(startLine == endLine);
					if (startLine != endLine) continue; /* Multiline structure ? */

					if (Lines.count(endLine)) {
						_RW.InsertText(D->getLocation(), "\n", true, true);
						Lines.insert(endLine+1);
					} else {
						Lines.insert(endLine);
					}
				}	
			}
		}
	}

	// void Initialize(ASTContext & Context) {
	// 	COSet.clear();
	// }

	// void Finalize(void) {
	// 	for (auto & CO : COSet) {
	// 		InstrumentSelect(CO);
	// 	}
	// }

	private:
		//std::set<ConditionalOperator *> COSet;
		Rewriter & _RW;
};


/* Implementation of the ASTConsumer interface for reading an AST produced
   by the Clang parser.
*/
class MyASTConsumer : public ASTConsumer {
	public:
		MyASTConsumer(Rewriter & RW) : _Visitor(RW) {}

		// void Initialize (ASTContext & Context) override {
		// 	_Visitor.Initialize(Context);
		// }

		/* Override the method that gets called for each parsed top-level declaration */
		bool HandleTopLevelDecl(DeclGroupRef DR) override {
			
			for (DeclGroupRef::iterator b = DR.begin(), e = DR.end(); b != e; ++b) {
				/* Traverse the declaration using our AST visitor */
				_Visitor.TraverseDecl(*b);
				//(*b)->dump();
			}
			//_Visitor.Finalize();
			return true;
		}

	private:
		MyASTVisitor _Visitor;
};

// For each source file provided to the tool, a new FrontendAction is created.
class MyFrontendAction : public ASTFrontendAction {
	public:
		MyFrontendAction() {}

		void EndSourceFileAction() override {
			//SourceManager &SM = _rewriter.getSourceMgr();
			//llvm::errs() << "** EndSourceFileAction for: " << SM.getFileEntryForID(SM.getMainFileID())->getName() << "\n";

			// Now emit the rewritten buffer.
			//_rewriter.getEditBuffer(SM.getMainFileID()).write(llvm::outs());
			ASSERT( _rewriter.overwriteChangedFiles() == false /* I know, pretty counter intuitive */ );
		}

		/* Not necessary for us */
		bool BeginInvocation (CompilerInstance &CI) override {
			return true;
		}		

		std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI, StringRef file) override {
			//llvm::errs() << "** Creating AST consumer for: " << file << "\n";
			_rewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
			return llvm::make_unique<MyASTConsumer>(_rewriter);
	  	}

	private:
		Rewriter _rewriter;
};


int main(int argc, const char *argv[]) {

	CommonOptionsParser op(argc, argv, MyToolCategory);
	ClangTool Tool (op.getCompilations(), op.getSourcePathList());
	// ClangTool::run accepts a FrontendActionFactory, which is then used to
	// create new objects implementing the FrontendAction interface. Here we use
	// the helper newFrontendActionFactory to create a default factory that will
	// return a new MyFrontendAction object every time.
	// To further customize this, we could create our own factory class.
	return Tool.run(newFrontendActionFactory<MyFrontendAction>().get());
}