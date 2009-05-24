//===- ABCPreProcess.h - -----------------------------------------------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
//===----------------------------------------------------------------------===//

#ifndef ABC_PREPROCESS_H
#define ABC_PREPROCESS_H

#include "safecode/SAFECode.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Utils/UnifyFunctionExitNodes.h"
#include "AffineExpressions.h"
#include "dsa/DSGraph.h"
#include "poolalloc/PoolAllocate.h"

using namespace llvm;

NAMESPACE_SC_BEGIN

Pass *createABCPreProcessPass();
  
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
      AU.addPreserved<PoolAllocateGroup>();
#if 0
      AU.addRequired<DominatorSet>();
      AU.addRequired<PostDominatorSet>();
      AU.addRequired<DominanceFrontier>();
      AU.addRequired<PostDominanceFrontier>();
#endif
      AU.setPreservesAll();
    }
    virtual bool runOnFunction(Function &F);
  };

NAMESPACE_SC_END
#endif
