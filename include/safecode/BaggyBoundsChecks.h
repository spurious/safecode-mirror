//===- BaggyBoundsChecks.h - Modify code for baggy bounds checks --------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// 
//
//===----------------------------------------------------------------------===//

#ifndef _BAGGY_BOUNDS_CHECKS_H_
#define _BAGGY_BOUNDS_CHECKS_H_

#include "llvm/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "safecode/SAFECode.h"
#include "safecode/Intrinsic.h"
#include "safecode/PoolHandles.h"

using namespace llvm;

NAMESPACE_SC_BEGIN

//
// Pass:InsertBaggyBoundsChecks 
//
// Description:
//  This pass aligns all globals and allocas.
//
struct InsertBaggyBoundsChecks : public ModulePass {
  public:
    static char ID;
    InsertBaggyBoundsChecks () : ModulePass ((intptr_t) &ID) { }
    const char *getPassName() const { return "Insert BaggyBounds Checks"; }
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      // Required passes
      AU.addRequired<TargetData>();
      AU.addRequired<InsertSCIntrinsic>();

      // Preserved passes
      AU.setPreservesAll();
    };

    // Visitor methods

  protected:
    // Pointers to required passes
    TargetData * TD;
    InsertSCIntrinsic *intrinsicPass;

};

NAMESPACE_SC_END
#endif
