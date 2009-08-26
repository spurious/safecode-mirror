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
// TODO:
//  There are several optimizations which may improve performance:
//
//  1) The old code did not insert calls to getActualValue() for pointers
//     compared against a NULL pointer.  We should determine that this
//     optimization is safe and re-enable it if it is safe.
//
//  2) We insert calls to getActualValue() even if the pointer is not checked
//     by a bounds check (and hence, is never rewritten).  It's a bit tricky,
//     but we should avoid rewriting a pointer back if its bounds check was
//     removed because the resulting pointer was always used in comparisons.
//
//  3) If done properly, all loads and stores to type-unknown objects have a
//     run-time check.  Therefore, we should only need OOB pointer rewriting on
//     type-known memory objects.
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
STATISTIC (Changes,    "Number of Bounds Checks Modified");
STATISTIC (GetActuals, "Number of getActualValue() Calls Inserted");

// Register the pass
static RegisterPass<RewriteOOB> P ("oob-rewriter",
                                   "OOB Pointer Rewrite Transform");

//
// Method: processFunction()
//
// Description:
//  This method searches for calls to a specified run-time check.  For every
//  such call, it replaces the pointer that the call checks with the return
//  value of the call.
//
//  This allows functions like boundscheck() to return a rewrite pointer;
//  this code changes the program to use the returned rewrite pointer instead
//  of the original pointer which was passed into boundscheck().
//
// Inputs:
//  F       - A pointer to the checking function to process.
//
// Return value:
//  false - No modifications were made to the Module.
//  true  - One or more modifications were made to the module.
//
bool
RewriteOOB::processFunction (Function * F) {
  //
  // Ensure the function has the right number of arguments and that its
  // result is a pointer type.
  //
  assert (isa<PointerType>(F->getReturnType()));

  //
  // To avoid recalculating the dominator information each time we process a
  // use of the specified function F, we will record the function containing
  // the call instruction to F and the corresponding dominator information; we
  // will then update this information only when the next use is a call
  // instruction belonging to a different function.  We are helped by the fact
  // that iterating through uses often groups uses within the same function.
  //
  Function * CurrentFunction = 0;
  DominatorTree * domTree = 0;

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
      // one because a call instruction's first operand is the function to
      // call.
      //
      std::set<Value *>Chain;
      Value * RealOperand = intrinPass->getValuePointer (CI);
      Value * PeeledOperand = peelCasts (RealOperand, Chain);

      //
      // Cast the result of the call instruction to match that of the original
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
      if ((CI->getParent()->getParent()) != CurrentFunction) {
        CurrentFunction = CI->getParent()->getParent();
        domTree = &getAnalysis<DominatorTree>(*CurrentFunction);
      }

      //
      // For every use that the call instruction dominates, change the use to
      // use the result of the call instruction.
      //
      Value::use_iterator UI = PeeledOperand->use_begin();
      for (; UI != PeeledOperand->use_end(); ++UI) {
        if (Instruction * Use = dyn_cast<Instruction>(UI))
          if ((CI != Use) && (domTree->dominates (CI, Use))) {
            UI->replaceUsesOfWith (PeeledOperand, CastCI);
            ++Changes;
          }
      }
    }
  }

  return modified;
}

//
// Method: addGetActualValues()
//
// Description:
//  Search for comparison or pointer to integer cast instructions which will
//  need to turn an OOB pointer back into the original pointer value.  Insert
//  calls to getActualValue() to do the conversion.
//
// Inputs:
//  M - The module in which to add calls to getActualValue().
//
// Return value:
//  true  - The module was modified.
//  false - The module was not modified.
//
bool
RewriteOOB::addGetActualValues (Module & M) {
  // Assume that we don't modify anything
  bool modified = false;

  for (Module::iterator F = M.begin(); F != M.end(); ++F) {
    for (inst_iterator I = inst_begin(F), E = inst_end(F); I != E; ++I) {
      if (ICmpInst *CmpI = dyn_cast<ICmpInst>(&*I)) {
        assert ((CmpI->getNumOperands() == 2) &&
                 "Compare instruction does not have two operands\n");
        //
        // Determine whether this is an integer comparison.
        //
        CmpInst::Predicate Pred = CmpI->getUnsignedPredicate();
        if ((Pred >= CmpInst::FIRST_ICMP_PREDICATE) &&
            (Pred <= CmpInst::LAST_ICMP_PREDICATE)) {
          //
          // Replace all pointer operands with a call to getActualValue().
          // This will convert an OOB pointer back into the real pointer value.
          //
          if (isa<PointerType>(CmpI->getOperand(0)->getType())) {
            // Rewrite both operands and flag that we modified the code
            addGetActualValue(CmpI, 0);
            modified = true;
          }

          if (isa<PointerType>(CmpI->getOperand(1)->getType())) {
            // Rewrite both operands and flag that we modified the code
            addGetActualValue(CmpI, 1);
            modified = true;
          }
        }
      }

      if (PtrToIntInst * CastInst = dyn_cast<PtrToIntInst>(&*I)) {
        //
        // Replace all pointer operands with a call to getActualValue().
        // This will convert an OOB pointer back into the real pointer value.
        //
        if (isa<PointerType>(CastInst->getOperand(0)->getType())) {
          // Rewrite both operands and flag that we modified the code
          addGetActualValue(CastInst, 0);
          modified = true;
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
//  SCI     - The instruction that has arguments requiring conversion.
//  operand - The index of the operand to the instruction that requires
//            conversion.
//
void
RewriteOOB::addGetActualValue (Instruction *SCI, unsigned operand) {
  //
  // Get a reference to the getactualvalue() function.
  //
  Function * GetActualValue = intrinPass->getIntrinsic("sc.get_actual_val").F;

  // We know that the operand is a pointer type 
  Value *op   = SCI->getOperand(operand);

  //
  // Peel casts off of the operand.
  //
  std::set<Value *>Chain;
  Value * peeledOp = peelCasts (op, Chain);

  //
  // Get the pool handle associated with the pointer.
  //
  Value *PH = 0;
  if (Argument *arg = dyn_cast<Argument>(peeledOp)) {
    Function * F = arg->getParent();
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
    PH = dsnPass->getPoolHandle(peeledOp, F, *FI);
  } else if (Instruction *Inst = dyn_cast<Instruction>(peeledOp)) {
    Function * F = Inst->getParent()->getParent();
    PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
    PH = dsnPass->getPoolHandle(peeledOp, F, *FI);
  } else if (isa<Constant>(peeledOp) || isa<AllocationInst>(peeledOp)) {
    //
    // Rewrite pointers are generated from calls to the SAFECode run-time
    // checks.  Therefore, constants and return values from allocation
    // functions are known to be the original value.
    //
    return;
  }

  if (!PH)
    std::cerr << "Error: No Pool Handle: " << *peeledOp << "\n" << std::endl;

  assert (PH && "addGetActualValue: No Pool Handle for operand!\n");

  //
  // If we have a pool handle, create a call to getActualValue() to convert
  // the pointer back to its original value.
  //
  if (PH) {
    //
    // Update the number of calls to getActualValue() that we inserted.
    //
    ++GetActuals;

    //
    // Insert the call to getActualValue()
    //
    const Type * VoidPtrType = getVoidPtrType();
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
  dsnPass    = &getAnalysis<DSNodePass>();
  paPass     = dsnPass->paPass;
  intrinPass = &getAnalysis<InsertSCIntrinsic>();

  //
  // Get the set of GEP checking functions
  //
  std::vector<Function *> GEPCheckingFunctions;
  InsertSCIntrinsic::intrinsic_const_iterator i, e;
  for (i = intrinPass->intrinsic_begin(), e = intrinPass->intrinsic_end(); i != e; ++i) {
    if (i->flag & InsertSCIntrinsic::SC_INTRINSIC_BOUNDSCHECK)
      GEPCheckingFunctions.push_back (i->F);
  }

  //
  // Insert calls so that comparison instructions convert Out of Bound pointers
  // back into their original values.  This should be done *before* rewriting
  // the program so that pointers are replaced with the return values of bounds
  // checks; this is because the return values of bounds checks have no DSNode
  // in the DSA results, and hence, no associated Pool Handle.
  //
  bool modified = addGetActualValues (M);

  //
  // Transform the code for each type of checking function.  Mark whether
  // we've changed anything.
  //
  while (GEPCheckingFunctions.size()) {
    // Remove a function from the set of functions to process
    Function * F = GEPCheckingFunctions.back();
    GEPCheckingFunctions.pop_back();

    //
    // Transform the function so that the pointer it checks is replaced with
    // its return value.  The return value is the rewritten OOB pointer.
    //
    modified |= processFunction (F);
  }
  return modified;
}

NAMESPACE_SC_END

