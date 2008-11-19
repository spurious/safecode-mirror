//===- OptimizeChecks.cpp - Optimize SAFECode Run-time Checks ------------- --//
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

#include "llvm/Analysis/Dominators.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "safecode/Config/config.h"

namespace llvm {

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
    bool processFunction (Module & M, std::string name, unsigned operand);

  public:
    static char ID;
    OptimizeChecks() : ModulePass((intptr_t)(&ID)) {}
    virtual bool runOnModule (Module & M);

    const char *getPassName() const {
      return "Optimize SAFECode Run-time Checks";
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      // Pretend that we don't modify anything
      AU.setPreservesAll();
    }
};
} 
#endif
