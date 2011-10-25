//===- InsertChecks.h - Insert run-time checks for SAFECode ------------------//
// 
//                     The LLVM Compiler Infrastructure
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

#ifndef _SAFECODE_LOADSTORECHECKS_H_
#define _SAFECODE_LOADSTORECHECKS_H_

#include "llvm/Pass.h"
#include "llvm/Support/InstVisitor.h"
#include "llvm/Target/TargetData.h"

namespace llvm {

//
// Pass: InsertLSChecks
//
// Description:
//  This pass inserts checks on load and store instructions.
//
struct InsertLSChecks : public FunctionPass, InstVisitor<InsertLSChecks> {
  public:
    static char ID;
    InsertLSChecks () : FunctionPass (ID) { }
    const char *getPassName() const { return "Insert Load/Store Checks"; }
    virtual bool  doInitialization (Module & M);
    virtual bool runOnFunction(Function & F);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      // Prerequisite passes
      AU.addRequired<TargetData>();

      // Preserve the CFG
      AU.setPreservesCFG();
    };

    // Visitor methods
    void visitLoadInst  (LoadInst  & LI);
    void visitStoreInst (StoreInst & SI);

  protected:
    // Pointer to load/store run-time check function
    Function * PoolCheckUI;
};

}
#endif
