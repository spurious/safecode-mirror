//===- ExactCheckOpt.cpp -------------------------------------------------- --//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
//  This pass tries to lower bounds checks and load/store checks to exact
//  checks, that is checks whose bounds information can be determined easily,
//  say, allocations inside a function or global variables. Therefore SAFECode
//  does not need to register stuffs in the meta-data.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "exactcheck-opt"

#include "llvm/ADT/Statistic.h"
#include "safecode/AllocatorInfo.h"
#include "safecode/OptimizeChecks.h"
#include "safecode/Utility.h"

#include <queue>

namespace llvm {

static RegisterPass<ExactCheckOpt> X ("exactcheck-opt", "Exact check optimization", true);

// Pass Statistics
namespace {
  STATISTIC (ExactChecks ,    "The number of checks lowered to exactcheck");
}

char ExactCheckOpt::ID = 0;

//
// Method: runOnModule()
//
// Description:
//  This method is the entry point for the transform pass.
//
// Inputs:
//  M - The LLVM module to transform.
//
// Return value:
//  true  - The module was modified.
//  false - The module was not modified.
//
bool
ExactCheckOpt::runOnModule(Module & M) {
  //
  // Add a prototype for the exactcheck function.
  //
  Type * VoidPtrTy = getVoidPtrType (M.getContext());
  Type * Int32Type = IntegerType::getInt32Ty(M.getContext());
  ExactCheck2 = (Function *) M.getOrInsertFunction ("exactcheck2",
                                                    VoidPtrTy,
                                                    VoidPtrTy,
                                                    VoidPtrTy,
                                                    Int32Type,
                                                    NULL);

  //
  // Scan through all the intrinsics and process those that perform run-time
  // checks.
  //
  for (unsigned index = 0; index < numChecks; ++index) {
    //
    // Clear the list of calls to intrinsics that must be removed.
    //
    checkingIntrinsicsToBeRemoved.clear();

    //
    // Scan through all uses of this run-time checking function and process
    // each call to it.
    //
    Function * F = M.getFunction (RuntimeChecks[index].name);
    if (F) {
      for (Value::use_iterator UI = F->use_begin(), E = F->use_end();
           UI != E;
           ++UI) {
        if (CallInst * CI = dyn_cast<CallInst>(*UI)) {
          visitCheckingIntrinsic(CI, RuntimeChecks[index]);
        }
      }
    }

    //
    // Update statistics if anything has changed.  We don't want to update
    // the statistics variable if nothing has happened because we don't want
    // it to appear in the output if it is zero.
    //
    if (checkingIntrinsicsToBeRemoved.size())
      ExactChecks += checkingIntrinsicsToBeRemoved.size();

    //
    // Remove checking intrinsics that have been optimized
    //
    for (std::vector<CallInst*>::const_iterator i = checkingIntrinsicsToBeRemoved.begin(), e = checkingIntrinsicsToBeRemoved.end(); i != e; ++i) {
      (*i)->eraseFromParent();
    }
  }

  //
  // Conservatively assume that we have changed something in the module.
  //
  return true;
}

//
// Function: findObject()
//
// Description:
//  Find the singular memory object to which this pointer points (if such a
//  singular object exists and is easy to find).
//
static Value *
findObject (Value * obj) {
  std::set<Value *> exploredObjects;
  std::set<Value *> objects;
  std::queue<Value *> queue;

  queue.push(obj);
  while (!queue.empty()) {
    Value * o = queue.front();
    queue.pop();
    if (exploredObjects.count(o)) continue;

    exploredObjects.insert(o);

    if (isa<CastInst>(o)) {
      queue.push(cast<CastInst>(o)->getOperand(0));
    } else if (isa<GetElementPtrInst>(o)) {
      queue.push(cast<GetElementPtrInst>(o)->getPointerOperand());
    } else if (isa<PHINode>(o)) {
      PHINode * p = cast<PHINode>(o);
      for(unsigned int i = 0; i < p->getNumIncomingValues(); ++i) {
        queue.push(p->getIncomingValue(i));
      }
    } else {
      objects.insert(o);
    }
  }
  return objects.size() == 1 ? *(objects.begin()) : NULL;
}

//
// Function: visitCheckingIntrinsic()
//
// Description:
//  Attempts to rewrite an extensive check into an efficient, accurate array
//  bounds check which will not use meta-data information.
//
// Inputs:
//  CI    - A pointer to the instruction that performs a run-time check.
//  Info  - A reference to a structure containing information on the
//         run-time check.
//
// Return value:
//  true  - Successfully rewrite the check into an exact check.
//  false - Cannot perform the optimization.
//
bool
ExactCheckOpt::visitCheckingIntrinsic (CallInst * CI, const struct CheckInfo & Info) {
  //
  // Get the pointer that is checked by this run-time check.
  //
  Value * CheckPtr = Info.getCheckedPointer(CI)->stripPointerCasts();

  //
  // Try to find the source of the pointer.
  //
  Value * BasePtr = findObject (CheckPtr);
  if (!BasePtr) return false;

  //
  // If the call is to a memory checking function, then we cannot blindly
  // convert a check that operates on a heap object; the heap object might be
  // deallocated between the time it was allocated and the time of the check.
  // Other checks can be converted since they don't try to detect dangling
  // pointers.
  //
  // So, if this is a memory check, make sure that the object cannot be freed
  // before the check.  Global variables and stack allocations cannot be freed.
  //
  if ((!(Info.isMemcheck)) ||
      ((isa<AllocaInst>(BasePtr)) || isa<GlobalVariable>(BasePtr))) {
    //
    // Attempt to get the size of the pointer.  If a size is returned, we know
    // that the base pointer points to the beginning of an object, and we can
    // do a run-time check without a lookup.
    //
    AllocatorInfoPass & AIP = getAnalysis<AllocatorInfoPass>();
    if (Value * Size = AIP.getObjectSize(BasePtr)) {
      rewriteToExactCheck(CI, BasePtr, CheckPtr, Size);
      return true;
    }
  }

  //
  // We were not able to insert a call to exactcheck().
  //
  return false;
}

//
// Function: rewriteToExactCheck()
//
// Description:
//  Rewrite a check into an exact check
//
// Inputs:
//  BasePointer   - An LLVM Value representing the base of the object to check.
//  Result        - An LLVM Value representing the pointer to check.
//  Bounds        - An LLVM Value representing the bounds of the check.
//
void
ExactCheckOpt::rewriteToExactCheck(CallInst * CI, Value * BasePointer, 
                                   Value * ResultPointer, Value * Bounds) {
  // The LLVM type for a void *
  Type *VoidPtrType = getVoidPtrType(CI->getContext()); 
  Type * Int32Type = IntegerType::getInt32Ty(CI->getContext());

  //
  // For readability, make sure that both the base pointer and the result
  // pointer have names.
  //
  if (!(BasePointer->hasName())) BasePointer->setName("base");
  if (!(ResultPointer->hasName())) ResultPointer->setName("result");

  //
  // Cast the operands to the correct type.
  //
  if (BasePointer->getType() != VoidPtrType)
    BasePointer = castTo (BasePointer, VoidPtrType,
                          BasePointer->getName()+".ec.casted",
                          CI);

  if (ResultPointer->getType() != VoidPtrType)
    ResultPointer = castTo (ResultPointer, VoidPtrType,
                            ResultPointer->getName()+".ec.casted",
                            CI);

  Value * CastBounds = Bounds;
  if (Bounds->getType() != Int32Type) {
    LLVMContext & Context = Int32Type->getContext();
    CastBounds = CastInst::CreateIntegerCast (CastBounds,
                                              Type::getInt32Ty(Context),
                                              false,
                                              CastBounds->getName(),
                                              CI);
  }

  //
  // Create the call to exactcheck2().
  //
  std::vector<Value *> args(1, BasePointer);
  args.push_back(ResultPointer);
  args.push_back(CastBounds);

  CallInst * ExactCheckCI = CallInst::Create (ExactCheck2, args, "", CI);
  // boundscheck / exactcheck return an out of bound pointer when REWRITE_OOB is
  // enabled. We need to replace all uses to make the optimization correct, but
  // we don't need do anything for load / store checks.
  //
  // We can test the condition above by simply testing the return types of the
  // checking functions.
  if (ExactCheckCI->getType() == CI->getType()) {
    CI->replaceAllUsesWith(ExactCheckCI);
  }

  checkingIntrinsicsToBeRemoved.push_back(CI);
}

}
