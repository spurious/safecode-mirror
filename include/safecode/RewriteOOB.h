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
#include "safecode/Config/config.h"

namespace llvm {

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
    bool processFunction (Module & M, std::string name, unsigned operand);

  public:
    static char ID;
    RewriteOOB() : ModulePass((intptr_t)(&ID)) {}
    const char *getPassName() const { return "Rewrite OOB Pass"; }
    virtual bool runOnModule (Module & M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      // We require Dominator information
      AU.addRequired<DominatorTree>();

      // Pretend that we don't modify anything
      AU.setPreservesAll();
    }
};
} 
#endif
