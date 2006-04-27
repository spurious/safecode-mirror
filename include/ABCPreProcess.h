#ifndef ABC_PREPROCESS_H
#define ABC_PREPROCESS_H

#include "llvm/Pass.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"
#include "AffineExpressions.h"
namespace llvm {

Pass *createABCPreProcessPass();
  
namespace ABC {
//This pass is written because the induction var pass  doesnt run properly 
//after the phi nodes are inserted.
 struct ABCPreProcess : public FunctionPass {
  private:
  PostDominanceFrontier * pdf;
  DominanceFrontier * df;
  DominatorSet *ds;
  PostDominatorSet *pds;
  void print(ostream &out);
  void indVariables(Loop *L);

  public :
    const char *getPassName() const { return "Collect Induction Variables"; }
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<LoopInfo>();
      AU.addRequired<DominatorSet>();
      AU.addRequired<PostDominatorSet>();
      AU.addRequired<DominanceFrontier>();
      AU.addRequired<PostDominanceFrontier>();
    }
    virtual bool runOnFunction(Function &F);
  };
}

}
#endif
