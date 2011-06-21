//===- CompleteChecks.h - Make run-time checks Complete ----------------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file defines a pass that modifies SAFECode run-time checks to be
// complete.  A complete check is for memory objects that are complete analyzed
// by SAFECode; if the run-time check fails, we *know* that it is an error.
//
//===----------------------------------------------------------------------===//

#ifndef _SAFECODE_COMPLETE_CHECKS_H_
#define _SAFECODE_COMPLETE_CHECKS_H_

#include "safecode/SAFECode.h"
#include "safecode/Intrinsic.h"

#include "poolalloc/PoolAllocate.h"

#include "llvm/Pass.h"

NAMESPACE_SC_BEGIN

//
// Pass: CompleteChecks
//
// Description:
//  This pass searches for SAFECode run-time checks.  If the checks are on
//  complete DSNodes, then it modifies the check to use a complete version of
//  the run-time check function.
//
struct CompleteChecks : public ModulePass {
  public:
    static char ID;
    CompleteChecks () : ModulePass ((intptr_t) &ID) { }
    const char *getPassName() const { return "Complete Run-time Checks"; }
    virtual bool runOnModule (Module & M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      // Required passes
      AU.addRequired<TargetData>();
      AU.addRequired<InsertSCIntrinsic>();
      AU.addRequired<EQTDDataStructures>();

      // Preserved passes
      AU.addPreserved<InsertSCIntrinsic>();
      AU.setPreservesCFG();
    };

  protected:
    // Pointers to required passes
    InsertSCIntrinsic * intrinsic;

    // Protected methods
    DSNodeHandle getDSNodeHandle (const Value * V, const Function * F);
    void makeComplete (Function * Complete, Function * Incomplete);
    void makeCStdLibCallsComplete(Function *, unsigned);
    void makeFSParameterCallsComplete(Module &M);
};

NAMESPACE_SC_END
#endif
