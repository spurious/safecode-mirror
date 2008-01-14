#ifndef BOTTOMUP_CALLGRAPH_H
#define BOTTOMUP_CALLGRAPH_H

#include "dsa/DataStructure.h"
#include "dsa/DSSupport.h"
#include "llvm/ADT/hash_map"
#include "llvm/Pass.h"
#include "llvm/Function.h"
#include "llvm/Module.h"

#include <set> 

namespace llvm {
  struct BottomUpCallGraph : public ModulePass {
    private:
    // OneCalledFunction - For each indirect function call, we keep track of one
    // the DSNode and the corresponding Call Instruction
    typedef hash_multimap<DSNode*, CallSite> CalleeNodeCallSiteMapTy;
    CalleeNodeCallSiteMapTy CalleeNodeCallSiteMap ;
    std::vector<Function *> Stack;
    std::set<Function *> Visited;
    void figureOutSCCs(Module &M);
    void visit(Function *f);

    public:
    
    static char ID;
    BottomUpCallGraph () : ModulePass ((intptr_t) &ID) {}
    const char *getPassName() const { return "Bottom-Up Call Graph"; }

    //This keeps the map of a function and its call sites in all the callers
    //including the indirectly called sites
    std::map<Function *, std::vector<CallSite> > FuncCallSiteMap;
    std::set<Function *> SccList; //Fns involved in Sccs
    
    bool isInSCC(Function *f) {
      return (SccList.find(f) != SccList.end());
    }
    
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<CompleteBUDataStructures>();
      AU.setPreservesAll();	
    }
    virtual bool runOnModule(Module&M);
  };
}
#endif
