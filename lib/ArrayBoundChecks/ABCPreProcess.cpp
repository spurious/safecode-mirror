//===- ArrayBoundCheck.cpp - ArrayBounds Checking (Omega)----------------===//
//
// requires piNodeinsertion pass before
//===----------------------------------------------------------------------===//

#include "llvm/Pass.h"
#include "llvm/Module.h"
#include "llvm/Function.h"
#include "llvm/BasicBlock.h"
#include "AffineExpressions.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/Instruction.h"
#include "llvm/Constants.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"

namespace ABC {

  IndVarMap indMap;
  ExitNodeMap enMap;
  PostDominanceFrontier * pdf;
  DominatorSet *ds;
  PostDominatorSet *pds;
  
  //This pass is written because the induction var pass doesnt run properly 
  //after the phi nodes are inserted.
  struct ABCPreProcess : public FunctionPass {
  private:
    void print(ostream &out);

  public :
    const char *getPassName() const { return "Collect Induction Variables"; }
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<UnifyFunctionExitNodes>();
      AU.addRequired<LoopInfo>();
      AU.addRequired<DominatorSet>();
      AU.addRequired<PostDominatorSet>();
      AU.addRequired<PostDominanceFrontier>();
    }
    virtual bool runOnFunction(Function &F);
  };




void ABCPreProcess::print(ostream &out) {
  out << " Printing phi nodes which are induction variables ... \n";
  IndVarMap::iterator ivCurrent = indMap.begin(), ivEnd = indMap.end();
  for (; ivCurrent != ivEnd; ++ivCurrent) {
    out << ivCurrent->first;
  }
  out << " Printing induction variables done ... \n";

}

bool ABCPreProcess::runOnFunction(Function &F) {
  BasicBlock *bb  = getAnalysis<UnifyFunctionExitNodes>().getReturnBlock();
  enMap[&F] = bb;
  LoopInfo &LI1 =  getAnalysis<LoopInfo>();
  LoopInfo *LI = & LI1;

  pdf = &getAnalysis<PostDominanceFrontier>();
  ds = &getAnalysis<DominatorSet>();
  pds = &getAnalysis<PostDominatorSet>();
  //  std::cerr << "calculated pdf for " << F.getName() << "\n";
  for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
    if (PHINode *phi = dyn_cast<PHINode>(*I)) {
      InductionVariable* iv = new InductionVariable(phi,LI);
      indMap[phi] = iv;
    }
  }
  //  print(std::cerr);
  return false;
}

  RegisterOpt<ABCPreProcess> Y("abcpre",
                              "Array Bounds Checking preprocess pass");

}

Pass *createABCPreProcessPass() { return new ABC::ABCPreProcess(); }
