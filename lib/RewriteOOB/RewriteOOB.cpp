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
#include <set>

char llvm::RewriteOOB::ID = 0;

namespace {
  STATISTIC (Changes, "Number of Bounds Checks Modified");
}

namespace llvm {
  
  //
  // Function: peelCasts()
  //
  // Description:
  //  This method peels off casts to get to the original instruction that
  //  generated the value for the given instruction.
  //
  // Inputs:
  //  PointerOperand - The value off of which we will peel the casts.
  //
  // Outputs:
  //  Chain - The set of values that are between the original value and the
  //          specified value.
  //
  // Return value:
  //  A pointer to the LLVM value that originates the specified LLVM value.
  //
  static Value *
  peelCasts (Value * PointerOperand, std::set<Value *> & Chain) {
    Value * SourcePointer = PointerOperand;
    bool done = false;

    while (!done) {
      //
      // Trace through constant cast and GEP expressions
      //
      if (ConstantExpr * cExpr = dyn_cast<ConstantExpr>(SourcePointer)) {
        if (cExpr->isCast()) {
          if (isa<PointerType>(cExpr->getOperand(0)->getType())) {
            Chain.insert (SourcePointer);
            SourcePointer = cExpr->getOperand(0);
            continue;
          }
        }

        // We cannot handle this expression; break out of the loop
        break;
      }

      //
      // Trace back through cast instructions.
      //
      if (CastInst * CastI = dyn_cast<CastInst>(SourcePointer)) {
        if (isa<PointerType>(CastI->getOperand(0)->getType())) {
          Chain.insert (SourcePointer);
          SourcePointer = CastI->getOperand(0);
          continue;
        }
        break;
      }

      // We can't scan through any more instructions; give up
      done = true;
    }

    return SourcePointer;
  }

  //
  // Method: processFunction()
  //
  // Description:
  //  If the specified function exists within the program, modify it so that
  //  the operand at the specified index is replaced with the return value
  //  of the function.
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
    for (Value::use_iterator FU = F->use_begin(); FU != F->use_end(); ++FU) {
      //
      // We are only concerned about call instructions; any other use is of
      // no interest to the organization.
      //
      if (CallInst * CI = dyn_cast<CallInst>(FU)) {
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
  }

  bool
  RewriteOOB::runOnModule (Module & M) {
    bool modified = false;
    modified |= processFunction (M, "boundscheck",   2);
    modified |= processFunction (M, "boundscheckui", 2);

    return modified;
  }
}

