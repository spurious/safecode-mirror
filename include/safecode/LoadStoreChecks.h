//===- InsertChecks.h - Insert run-time checks for SAFECode ------------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements several passes that insert run-time checks to enforce
// SAFECode's memory safety guarantees as well as several other passes that
// help to optimize the instrumentation.
//
//===----------------------------------------------------------------------===//

#ifndef _SAFECODE_INSERT_CHECKS_H_
#define _SAFECODE_INSERT_CHECKS_H_

#include "safecode/SAFECode.h"

#include "llvm/Pass.h"
#include "llvm/Support/InstVisitor.h"

#include "safecode/Intrinsic.h"

NAMESPACE_SC_BEGIN

//
// Pass: InsertLSChecks
//
// Description:
//  This pass inserts checks on load and store instructions.
//
struct InsertLSChecks : public FunctionPass, InstVisitor<InsertLSChecks> {
  public:
    static char ID;
    InsertLSChecks () : FunctionPass ((intptr_t) &ID) { }
    const char *getPassName() const { return "Insert Load/Store Checks"; }
    virtual bool runOnFunction(Function &F);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      // Required passes
      AU.addRequired<TargetData>();

      // Preserved passes
      AU.addPreserved<InsertSCIntrinsic>();
      AU.setPreservesCFG();
    };

    // Visitor methods
    void visitLoadInst  (LoadInst  & LI);
    void visitStoreInst (StoreInst & SI);

  protected:
    // Pointers to required passes
    TargetData * TD;

    // Pointer to load/store run-time check function
    Function * PoolCheckUI;
};


NAMESPACE_SC_END
#endif
