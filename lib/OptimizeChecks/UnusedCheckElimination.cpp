//===- OptimizeChecks.cpp - Optimize SAFECode Checks ---------------------- --//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass eliminates unused checks.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "opt-safecode"

#include "llvm/ADT/Statistic.h"
#include "llvm/Constants.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"

#include "safecode/OptimizeChecks.h"
#include "SCUtils.h"

#include <iostream>
#include <set>

namespace {
  struct DeleteItself {
    void operator()(llvm::CallInst * CI) {
      CI->eraseFromParent();
    }
  };
}

NAMESPACE_SC_BEGIN

char UnusedCheckElimination::ID = 0;

static RegisterPass<UnusedCheckElimination> X ("unused-check-elim", "Unused Check elimination");


bool
UnusedCheckElimination::runOnModule (Module & M) {
  //
  // Get prerequisite analysis results.
  //
  unusedChecks.clear();
  intrinsic = &getAnalysis<InsertSCIntrinsic>();

  InsertSCIntrinsic::intrinsic_const_iterator i, e;
  for (i = intrinsic->intrinsic_begin(), e = intrinsic->intrinsic_end(); i != e; ++i) {
    if (i->flag & (InsertSCIntrinsic::SC_INTRINSIC_CHECK | InsertSCIntrinsic::SC_INTRINSIC_OOB)) {
      for (Value::use_iterator I = i->F->use_begin(), E = i->F->use_end(); I != E; ++I) {
        CallInst * CI = cast<CallInst>(*I);
        if (intrinsic->getValuePointer(CI)->use_empty()) unusedChecks.push_back(CI);
      }
    }
  }

  DeleteItself op;
  std::for_each(unusedChecks.begin(), unusedChecks.end(), op);

  bool modified = unusedChecks.size() > 0;
  unusedChecks.clear();
  return modified;
}

NAMESPACE_SC_END

