//===- OptimizeChecks.cpp - Optimize SAFECode Checks ---------------------- --//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass performs optimizations on the SAFECode checks.
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

char NAMESPACE_SC::OptimizeChecks::ID = 0;

namespace {
  STATISTIC (Removed, "Number of Bounds Checks Removed");
}

NAMESPACE_SC_BEGIN

//
// Function: onlyUsedInCompares()
//
// Description:
//  Determine whether the result of the given instruction is only used in
//  comparisons.
//
// Return value:
//  true  - The instruction is only used in comparisons.
//  false - The instruction has some other use besides comparisons.
//
static bool
onlyUsedInCompares (Value * Val) {
  // The worklist
  std::vector<Value *> Worklist;

  // The set of processed values
  std::set<Value *> Processed;

  //
  // Initialize the worklist.
  //
  Worklist.push_back(Val);

  //
  // Process each item in the work list.
  //
  while (Worklist.size()) {
    Value * V = Worklist.back();
    Worklist.pop_back();

    //
    // Check whether we have already processed this value.  If not, mark it as
    // processed.
    //
    if (Processed.find (V) != Processed.end()) continue;
    Processed.insert (V);

    //
    // Scan through all the uses of this value.  Some uses may be safe.  Other
    // uses may generate uses we need to check.  Still others are known-bad
    // uses.  Handle each appropriately.
    //
    for (Value::use_iterator U = V->use_begin(); U != V->use_end(); ++U) {
      // Compares are okay
      if (isa<CmpInst>(U)) continue;

      // Casts require that we check the result, too.
      if (isa<CastInst>(U)) {
        Worklist.push_back(*U);
        continue;
      }

      // Phi nodes require that we check the result, too.
      if (isa<PHINode>(U)) {
        Worklist.push_back(*U);
        continue;
      }

      // Calls to run-time functions are okay; others are not.
      if (CallInst * CI = dyn_cast<CallInst>(U)) {
        if (Function * F = CI->getCalledFunction()) {
          std::string name = F->getName();
          if ((name == "boundscheck") || (name == "boundscheckui") ||
              (name == "exactcheck2")) {
            continue;
          }
        }
      }

      //
      // We don't know what this is; just assume it is bad.
      //
      return false;
    }
  }

  //
  // All uses are comparisons.  Return true.
  //
  return true;
}

//
// Method: processFunction()
//
// Description:
//  Look for calls of the specified function (which is a SAFECode run-time
//  check), determine whether the call can be eliminated, and eliminate it
//  if possible.
//
// Inputs:
//  M       - The module in which to search for the function.
//  name    - The name of the function.
//  operand - The index of the operand that represents the value being
//            checked.
//
// Return value:
//  false - No modifications were made to the Module.
//  true  - One or more modifications were made to the module.
//
bool
OptimizeChecks::processFunction (Module & M,
                                 std::string name,
                                 unsigned operand) {
  //
  // Get the reference to the function.  If the function doesn't exist, then
  // no modifications are necessary.
  //
  Function * F = M.getFunction (name);
  if (!F) return false;

  //
  // Ensure the function has the right number of arguments and that its
  // result is a pointer type.
  //
  assert (operand < (F->getFunctionType()->getNumParams()));
  assert (isa<PointerType>(F->getReturnType()));

  //
  // Iterate though all calls to the function and determine whether the
  // specified is only used in comparisons.  If so, then schedule the check
  // (i.e., the call) for removal.
  //
  bool modified = false;
  std::vector<Instruction *> CallsToDelete;
  for (Value::use_iterator FU = F->use_begin(); FU != F->use_end(); ++FU) {
    //
    // We are only concerned about call instructions; any other use is of
    // no interest to the organization.
    //
    if (CallInst * CI = dyn_cast<CallInst>(FU)) {
      //
      // If the call instruction has any uses, we cannot remove it.
      //
      if (CI->use_begin() != CI->use_end()) continue;

      //
      // Get the operand that needs to be replaced as well as the operand
      // with all of the casts peeled away.  Increment the operand index by
      // one because a call instruction's first operand is the function to
      // call.
      //
      std::set<Value *>Chain;
      Value * Operand = peelCasts (CI->getOperand(operand+1), Chain);

      //
      // If the operand is only used in comparisons, mark the run-time check
      // for removal.
      //
      if (onlyUsedInCompares (Operand)) {
        CallsToDelete.push_back (CI);
        ++Removed;
        modified = true;
      }
    }
  }

  //
  // Remove all of the instructions that we found to be unnecessary.
  //
  while (CallsToDelete.size()) {
    Instruction * I = CallsToDelete.back();
    CallsToDelete.pop_back();
    I->eraseFromParent();
  }

  return modified;
}

bool
OptimizeChecks::runOnModule (Module & M) {
  bool modified = false;
  modified |= processFunction (M, "boundscheck",   2);
  modified |= processFunction (M, "boundscheckui", 2);
  modified |= processFunction (M, "exactcheck2",   1);

  return modified;
}

NAMESPACE_SC_END

