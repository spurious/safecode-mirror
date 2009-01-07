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

#define DEBUG_TYPE "rewrite-OOB"

#include "llvm/ADT/Statistic.h"
#include "llvm/Constants.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"

#include "safecode/RewriteOOB.h"
#include "SCUtils.h"

#include <iostream>

char llvm::RewriteOOB::ID = 0;

namespace {
  STATISTIC (Changes, "Number of Bounds Checks Modified");
}

namespace llvm {
  
  //
  // Method: processFunction()
  //
  // Description:
  //  This method searches for calls to a specified function.  For every such
  //  call, it replaces the use of an operand of the call with the return value
  //  of the call.
  //
  //  This allows functions like boundscheck() to return a rewrite pointer;
  //  this code changes the program to use the returned rewrite pointer instead
  //  of the original pointer which was passed into boundscheck().
  //
  // Inputs:
  //  M       - The module in which to search for the function.
  //  name    - The name of the function.
  //  operand - The index of the operand that should be replaced.
  //
  // Return value:
  //  false - No modifications were made to the Module.
  //  true  - One or more modifications were made to the module.
  //
  bool
  RewriteOOB::processFunction (Module & M, std::string name, unsigned operand) {
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
    // Iterate though all calls to the function and modify the use of the
    // operand to be the result of the function.
    //
    bool modified = false;
    for (Value::use_iterator FU = F->use_begin(); FU != F->use_end(); ++FU) {
      //
      // We are only concerned about call instructions; any other use is of
      // no interest to the organization.
      //
      if (CallInst * CI = dyn_cast<CallInst>(FU)) {
        //
        // We're going to make a change.  Mark that we will have done so.
        //
        modified = true;

        //
        // Get the operand that needs to be replaced as well as the operand
        // with all of the casts peeled away.  Increment the operand index by
        // one because a call instrution's first operand is the function to
        // call.
        //
        std::set<Value *>Chain;
        Value * RealOperand = CI->getOperand(operand+1);
        Value * PeeledOperand = peelCasts (RealOperand, Chain);

        //
        // Cast the result of the call instruction to match that of the orignal
        // value.
        //
        BasicBlock::iterator i(CI);
        Instruction * CastCI = castTo (CI,
                                       PeeledOperand->getType(),
                                       PeeledOperand->getName(),
                                       ++i);

        //
        // Get dominator information for the function.
        //
        Function * F = CI->getParent()->getParent();
        DominatorTree & domTree = getAnalysis<DominatorTree>(*F);

        //
        // For every use that the call instruction dominates, change the use to
        // use the result of the call instruction.
        //
        Value::use_iterator UI = PeeledOperand->use_begin();
        for (; UI != PeeledOperand->use_end(); ++UI) {
          if (Instruction * Use = dyn_cast<Instruction>(UI))
            if ((CI != Use) && (domTree.dominates (CI, Use))) {
              UI->replaceUsesOfWith (PeeledOperand, CastCI);
              ++Changes;
            }
        }
      }
    }

    return modified;
  }

  bool
  RewriteOOB::runOnModule (Module & M) {
    bool modified = false;
    modified |= processFunction (M, "boundscheck",   2);
    modified |= processFunction (M, "boundscheckui", 2);

    return modified;
  }
}

