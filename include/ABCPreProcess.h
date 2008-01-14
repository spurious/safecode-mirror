#ifndef ABC_PREPROCESS_H
#define ABC_PREPROCESS_H

#include "llvm/Pass.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"
#include "AffineExpressions.h"
#include "poolalloc/PoolAllocate.h"

namespace llvm {

Pass *createABCPreProcessPass();
  
namespace ABC {
//This pass is written because the induction var pass  doesnt run properly 
//after the phi nodes are inserted.
 struct ABCPreProcess : public FunctionPass {
  private:
#if 0
  PostDominanceFrontier * pdf;
  DominanceFrontier * df;
  DominatorSet *ds;
  PostDominatorSet *pds;
#endif
  virtual void print(ostream &out, const Module * M) const;
  void indVariables(Loop *L);

  public :
    static char ID;
    ABCPreProcess () : FunctionPass ((intptr_t) &ID) {}
    const char *getPassName() const { return "Collect Induction Variables"; }
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<LoopInfo>();
      AU.addPreserved<PoolAllocate>();
#if 0
      AU.addRequired<DominatorSet>();
      AU.addRequired<PostDominatorSet>();
      AU.addRequired<DominanceFrontier>();
      AU.addRequired<PostDominanceFrontier>();
#endif
    }
    virtual bool runOnFunction(Function &F);
  };
}
}
#endif
