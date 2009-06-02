//===- OptimizeChecks.cpp - Optimize SAFECode Run-time Checks -----*- C++ -*--//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass optimizes some of the SAFECode run-time checks.
//
//===----------------------------------------------------------------------===//

#ifndef OPTIMIZECHECKS_H
#define OPTIMIZECHECKS_H

#include "safecode/SAFECode.h"
#include "safecode/Intrinsic.h"
#include "safecode/PoolHandles.h"

#include "llvm/Analysis/Dominators.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"

using namespace llvm;

NAMESPACE_SC_BEGIN

//
// FIXME: Obviously a renamed is required!
// Pass: OptimizeChecks
//
// Description:
//  This pass examines the run-time checks that SAFECode has inserted into a
//  program and attempts to remove checks that are unnecessary.
//
struct OptimizeChecks : public ModulePass {
  private:
    // Private methods
    bool processFunction (Function * F);
    bool onlyUsedInCompares (Value * Val);

    // References to required analysis passes
    InsertSCIntrinsic * intrinPass;

    // The set of GEP checking functions
    std::vector<Function *> GEPCheckingFunctions;

  public:
    static char ID;
    OptimizeChecks() : ModulePass((intptr_t)(&ID)) {}
    virtual bool runOnModule (Module & M);

    const char *getPassName() const {
      return "Optimize SAFECode Run-time Checks";
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      // We need to know about SAFECode intrinsics
      AU.addRequired<InsertSCIntrinsic>();

      // Pretend that we don't modify anything
      AU.setPreservesAll();
    }
};

//
// Pass: ExactCheckOpt
//
// Description:
//  This pass tries to lower bounds checks and load/store checks to exact
//  checks, that is checks whose bounds information can be determined easily,
//  say, allocations inside a function or global variables. Therefore SAFECode
//  does not need to register stuffs in the meta-data.
//
struct ExactCheckOpt : public ModulePass {
 public:
  static char ID;
 ExactCheckOpt() : ModulePass((intptr_t)(&ID)) {}
  virtual bool runOnModule (Module & M);
  const char *getPassName() const {
    return "Exact check optimization";
  }
  
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    // We need to know about SAFECode intrinsics
    AU.addRequired<InsertSCIntrinsic>();
    AU.addRequired<DSNodePass>();
    // Pretend that we don't modify anything
    AU.setPreservesAll();
  }

 private:
    // References to required analysis passes
    InsertSCIntrinsic * intrinsic;
    Function *ExactCheck2;
    bool visitCheckingIntrinsic(CallInst * CI);
    void rewriteToExactCheck(CallInst * CI, Value * BasePointer, 
                             Value * ResultPointer, Value * Bounds);
    Value * getBasePtr (Value * PointerOperand, bool & indexed);
    std::vector<CallInst*> checkingIntrinsicsToBeRemoved;
};

NAMESPACE_SC_END

#endif
