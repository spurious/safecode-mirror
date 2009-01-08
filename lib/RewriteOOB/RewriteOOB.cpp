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
#include "llvm/InstrTypes.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/Support/InstIterator.h"

#include "safecode/RewriteOOB.h"
#include "SCUtils.h"

#include <iostream>


NAMESPACE_SC_BEGIN

// Identifier variable for the pass
char RewriteOOB::ID = 0;

// Statistics
STATISTIC (Changes, "Number of Bounds Checks Modified");

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
RewriteOOB::addGetActualValues (Module & M) {
  // Assume that we don't modify anything
  bool modified = false;
  for (Module::iterator F = M.begin(); F != M.end(); ++F) {
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      if (ICmpInst *CmpI = dyn_cast<ICmpInst>(&*I)) {
        switch (CmpI->getUnsignedPredicate()) {
          case CmpInst::ICMP_EQ:
          case CmpInst::ICMP_NE:
            // Replace all pointer operands with the getActualValue() call
            assert ((CmpI->getNumOperands() == 2) &&
                     "nmber of operands for CmpI different from 2 ");
            if (isa<PointerType>(CmpI->getOperand(0)->getType())) {
              // we need to insert a call to getactualvalue
              if ((!isa<ConstantPointerNull>(CmpI->getOperand(0))) &&
                  (!isa<ConstantPointerNull>(CmpI->getOperand(1)))) {
                addGetActualValue(CmpI, 0);
                addGetActualValue(CmpI, 1);

                // Flag that we modified the code
                modified = true;
              }
            }
            break;

          default:
            break;
        }
      }
    }
  }

  // Return whether we modified anything
  return modified;
}

//
// Method: addGetActualValue()
//
// Description:
//  Insert a call to the getactualvalue() run-time function to convert the
//  potentially Out of Bound pointer back into its original value.
//
// Inputs:
//  ICmpInst - The comparison instruction that has arguments requiring
//             conversion.
//  operand  - The index of the operand to the compare instruction that
//             requires conversion.
//
void
RewriteOOB::addGetActualValue (ICmpInst *SCI, unsigned operand) {
  //
  // Get a reference to the getactualvalue() function.
  //
  Function * GetActualValue = intrinPass->getIntrinsic("sc.get_actual_val").F;

  // We know that the operand is a pointer type 
  Value *op   = SCI->getOperand(operand);

  //
  // Get the pool handle associated with the pointer.
  //
  Value *PH = 0;
  if (Argument *arg = dyn_cast<Argument>(op)) {
    Function * F = arg->getParent();
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
    PH = dsnPass->getPoolHandle(op, F, *FI);
  } else if (Instruction *Inst = dyn_cast<Instruction>(op)) {
    Function * F = Inst->getParent()->getParent();
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
    PH = dsnPass->getPoolHandle(op, F, *FI);
  } else if (isa<Constant>(op)) {
    return;
    //      abort();
  } else if (!isa<ConstantPointerNull>(op)) {
    //has to be a global
    abort();
  }

  op = SCI->getOperand(operand);

  if (!isa<ConstantPointerNull>(op)) {
    if (PH) {
      if (1) { //HACK fixed
        const Type * VoidPtrType = PointerType::getUnqual(Type::Int8Ty);
        Value * PHVptr = castTo (PH, VoidPtrType, "castPH", SCI);
        Value * OpVptr = castTo (op,
                                 VoidPtrType,
                                 op->getName() + ".casted",
                                 SCI);

        std::vector<Value *> args = make_vector (PHVptr, OpVptr,0);
        CallInst *CI = CallInst::Create (GetActualValue,
                                         args.begin(),
                                         args.end(),
                                         "getval",
                                         SCI);
        Instruction *CastBack = castTo (CI,
                                        op->getType(),
                                        op->getName()+".castback",
                                        SCI);
        SCI->setOperand (operand, CastBack);
      }
    } else {
      //It shouldn't work if PH is not null
    }
  }
}

//
// Method: runOnModule()
//
// Description:
//  Entry point for this LLVM pass.
//
// Return value:
//  true  - The module was modified.
//  false - The module was not modified.
//
bool
RewriteOOB::runOnModule (Module & M) {
  //
  // Get prerequisite analysis results.
  //
  paPass     = &getAnalysis<PoolAllocateGroup>();
  dsnPass    = &getAnalysis<DSNodePass>();
  intrinPass = &getAnalysis<InsertSCIntrinsic>();

  //
  // Transform the code for each type of checking function.  Mark whether
  // we've changed anything.
  //
  bool modified = false;
  modified |= processFunction (M, "boundscheck",   2);
  modified |= processFunction (M, "boundscheckui", 2);

  //
  // Insert calls so that comparison instructions convert Out of Bound pointers
  // back into their original values.
  //
  modified |= addGetActualValues (M);
  return modified;
}

NAMESPACE_SC_END

