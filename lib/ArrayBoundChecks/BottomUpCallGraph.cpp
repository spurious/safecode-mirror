#include "dsa/DSGraph.h"
#include "dsa/DSNode.h"
#include "BottomUpCallGraph.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/CallSite.h"
#include <iostream>

using namespace llvm;

namespace llvm {
char BottomUpCallGraph::ID = 0;

//This is needed because some call sites get merged away during DSA if they have
//the same inputs for instance.
//But for array bounds checking we need to get constraints from all the call sites
//So we have to get them some how.

bool BottomUpCallGraph::runOnModule(Module &M) {
  CompleteBUDataStructures &CBU = getAnalysis<CompleteBUDataStructures>();
  const CompleteBUDataStructures::ActualCalleesTy &AC = CBU.getActualCallees();

  for (Module::iterator MI = M.begin(), ME = M.end(); MI != ME; ++MI) 
    for (inst_iterator I = inst_begin(MI), E = inst_end(MI); I != E; ++I) {
      if (Instruction *CI = dyn_cast<Instruction>(&*I)) {
	if (isa<CallInst>(CI)) {
	  CompleteBUDataStructures::callee_iterator cI = CBU.callee_begin(CI),
                                              cE = CBU.callee_end(CI);
	  if (cI == cE) {
	    //Hmm this call site is not included in the cbuds
	    //so we need to extra stuff.
	    CallSite CS = CallSite::get(CI);
	    if (Function *FCI = dyn_cast<Function>(CI->getOperand(0))) {
	      //if it is a direct call, we can just add it!
	      FuncCallSiteMap[FCI].push_back(CS);
	    } else {
	      //Here comes the ugly part
	      Function *parenFunc = CI->getParent()->getParent();
	      DSNode *calleeNode = CBU.getDSGraph(*parenFunc).getNodeForValue(CS.getCalledValue()).getNode();
	      CalleeNodeCallSiteMap.insert(std::make_pair(calleeNode, CS));
	    }
	  }
	}
      }
    }
  
  for (CompleteBUDataStructures::ActualCalleesTy::const_iterator I = AC.begin(),
         E = AC.end(); I != E; ++I) {
    CallSite CS = CallSite::get(I->first);
    DEBUG(std::cerr << "CALLEE: " << I->second->getName() << " from : " << I->first);
    FuncCallSiteMap[I->second].push_back(CS);
    //see if this is equivalent to any other callsites of this function.....
    //FIXME This is very expensive way of doing it, 
    Function *parenFunc = I->first->getParent()->getParent();
    DSNode *calleeNode = CBU.getDSGraph(*parenFunc).getNodeForValue(CS.getCalledValue()).getNode();
    CalleeNodeCallSiteMapTy::const_iterator cI, cE;
    tie(cI, cE) = CalleeNodeCallSiteMap.equal_range(calleeNode);
    for (; cI != cE; ++cI) {
      //all the call sites I->second should also be added to the funccallsitemap
      FuncCallSiteMap[I->second].push_back(cI->second);
    }
  }
  figureOutSCCs(M);
  return false;
}


void BottomUpCallGraph::visit(Function *f) {
  if (Visited.find(f) == Visited.end()) {
    //Have not visited it before
    Visited.insert(f);
    //not visited implies it won't be there on the stack anyways, so push it
    //on stack
    Stack.push_back(f);
    if (FuncCallSiteMap.count(f)) {
      std::vector<CallSite> & callsitelist = FuncCallSiteMap[f];
      for (unsigned idx = 0, sz = callsitelist.size(); idx != sz; ++idx) {
	Function *parent = callsitelist[idx].getInstruction()->getParent()->getParent();
	visit(parent);
      }
    }
    Stack.pop_back();
  } else {
    //Have already visited it, check if it forms SCC
    std::vector<Function*>::iterator res = std::find(Stack.begin(), Stack.end(), f);
    if (res != Stack.end()) {
      //Cycle detected.
      for (; res != Stack.end() ; ++res) {
	SccList.insert(*res);
      }
    }
  }
}

void BottomUpCallGraph::figureOutSCCs(Module &M) {
  for (Module::iterator I = M.begin(), E= M.end(); I != E ; ++I) {
    visit(I);
  }
}
}

  RegisterPass<BottomUpCallGraph> bucg("bucg","Call Graph from CBUDS");
