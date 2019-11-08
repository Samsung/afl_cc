#ifndef __STEENSGAARD_HH_
#define __STEENSGAARD_HH_
#include "dsa/DataStructure.h"

namespace llvm
{
  /// SteensgaardsDataStructures - Analysis that computes a context-insensitive
  /// data structure graphs for the whole program.
  ///
  class SteensgaardDataStructures : public DataStructures {
    DSGraph * ResultGraph;
    DataStructures * DS;
    void ResolveFunctionCall (const Function *F, const DSCallSite &Call,
                              DSNodeHandle &RetVal);
    bool runOnModuleInternal(Module &M);

  public:
    static char ID;
    SteensgaardDataStructures() : 
      DataStructures(ID, "steensgaard."),
      ResultGraph(NULL) {}
    virtual ~SteensgaardDataStructures() {}
    virtual bool runOnModule(Module &M);
    virtual void releaseMemory();

    virtual void getAnalysisUsage(AnalysisUsage &AU) const 
    {
      AU.addRequired<StdLibDataStructures>();
      AU.setPreservesAll();
    }
  
    /// getDSGraph - Return the data structure graph for the specified function.
    ///
    virtual DSGraph *getDSGraph(const Function& F) const 
    {
      return F.isDeclaration () ? NULL : getResultGraph();
    }
  
    virtual bool hasDSGraph(const Function& F) const {return !F.isDeclaration ();}
    virtual DSGraph* getOrCreateGraph (const Function *F) {return getResultGraph ();}
    

    /// getDSGraph - Return the data structure graph for the whole program.
    ///
    DSGraph *getResultGraph() const {return ResultGraph;}

    void print(llvm::raw_ostream &O, const Module *M) const;
  };

  extern char &SteensgaardDataStructuresID;  
}


#endif
