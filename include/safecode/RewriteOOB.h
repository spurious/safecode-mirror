//===- RewriteOOB.cpp - Rewrite Out of Bounds Pointers -------------------- --//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass performs necessary transformations to ensure that Out of Bound
// pointer rewrites work correctly.
//
//===----------------------------------------------------------------------===//

#ifndef REWRITEOOB_H
#define REWRITEOOB_H

#include "llvm/Analysis/Dominators.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"

#include "safecode/SAFECode.h"
#include "safecode/Intrinsic.h"
#include "safecode/PoolHandles.h"

using namespace llvm;

NAMESPACE_SC_BEGIN

//
// Pass: RewriteOOB
//
// Description:
//  This pass modifies a program so that it uses Out of Bounds pointer
//  rewriting.  This involves modifying all uses of a checked pointer to use
//  the return value of the run-time check.
//
struct RewriteOOB : public ModulePass {
  private:
    // Private methods
    bool processFunction (Function * F);
    bool addGetActualValues (Module & M);
    void addGetActualValue (ICmpInst *SCI, unsigned operand);

    // Private variables
    PoolAllocateGroup * paPass;
    DSNodePass        * dsnPass;
    InsertSCIntrinsic * intrinPass;

  public:
    static char ID;
    RewriteOOB() : ModulePass((intptr_t)(&ID)) {}
    const char *getPassName() const { return "Rewrite OOB Pass"; }
    virtual bool runOnModule (Module & M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      // We require Dominator information
      AU.addRequired<DominatorTree>();

      // We require these pass to get information on pool handles
      AU.addRequired<DSNodePass>();
      AU.addRequired<PoolAllocateGroup>();

      // This pass gives us information on the various run-time checks
      AU.addRequired<InsertSCIntrinsic>();

      // Require this pass to keep it from being invalidated
      AU.addRequiredTransitive<EQTDDataStructures>();

      // Pretend that we don't modify anything
      AU.setPreservesAll();
    }
};

NAMESPACE_SC_END

#endif
