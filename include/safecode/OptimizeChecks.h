//===- OptimizeChecks.cpp - Optimize SAFECode Run-time Checks -----*- C++ -*--//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass optimizes some of the SAFECode run-time checks.
//
//===----------------------------------------------------------------------===//

#ifndef SAFECODE_OPTIMIZECHECKS_H
#define SAFECODE_OPTIMIZECHECKS_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"

#include "safecode/CheckInfo.h"
#include "safecode/AllocatorInfo.h"

namespace llvm {

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
 ExactCheckOpt() : ModulePass(ID) {}
  virtual bool runOnModule (Module & M);
  const char *getPassName() const {
    return "Exact check optimization";
  }
  
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    // We need to know about SAFECode intrinsics
    AU.addRequired<AllocatorInfoPass>();
    AU.setPreservesCFG();
  }

 private:
    // References to required analysis passes
    Function *ExactCheck2;
    Function *FastLSCheck;
    bool visitCheckingIntrinsic(CallInst * CI, const struct CheckInfo & Info);
    void rewriteToExactCheck(bool isMemCheck, CallInst * CI,
                             Value * BasePointer, 
                             Value * ResultPointer,
                             Value * ResultLength,
                             Value * Bounds);
    std::vector<CallInst*> checkingIntrinsicsToBeRemoved;
};

//
// Pass: OptimizeChecks
//
// Description:
//  This pass examines the run-time checks that SAFECode has inserted into a
//  program and attempts to remove checks that are unnecessary.
//
struct OptimizeChecks : public ModulePass {
  private:
    // Private methods
    bool processFunction (Module & M, const struct CheckInfo & Info);
    bool onlyUsedInCompares (Value * Val);

  public:
    static char ID;
    OptimizeChecks() : ModulePass(ID) {}
    virtual bool runOnModule (Module & M);

    const char *getPassName() const {
      return "Optimize SAFECode Run-time Checks";
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesCFG();
    }
};

}

#endif
