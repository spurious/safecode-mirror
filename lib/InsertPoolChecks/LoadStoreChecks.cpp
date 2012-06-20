//===- LoadStoreChecks.cpp - Insert load/store run-time checks ------------ --//
// 
//                     The LLVM Compiler Infrastructure
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass instruments loads and stores with run-time checks to ensure memory
// safety.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "safecode"

#include "llvm/ADT/Statistic.h"
#include "llvm/Attributes.h"
#include "llvm/Constants.h"
#include "safecode/LoadStoreChecks.h"
#include "safecode/Utility.h"

namespace llvm {

char InsertLSChecks::ID = 0;

static RegisterPass<InsertLSChecks>
X ("lschecks", "Insert load/store run-time checks");

// Pass Statistics
namespace {
  STATISTIC (LSChecks, "Load/Store Checks Added");
}

//
// Method: isTriviallySafe()
//
// Description:
//  This method determines if a memory access of the specified type is safe
//  (and therefore does not need a run-time check).
//
// Inputs:
//  Ptr     - The pointer value that is being checked.
//  MemType - The type of the memory access.
//
// Return value:
//  true  - The memory access is safe and needs no run-time check.
//  false - The memory access may be unsafe and needs a run-time check.
//
// FIXME:
//  Performing this check here really breaks the separation of concerns design
//  that we try to follow; this should really be implemented as a separate
//  optimization pass.  That said, it is quicker to implement it here.
//
bool
InsertLSChecks::isTriviallySafe (Value * Ptr, Type * MemType) {
  //
  // Attempt to see if this is a stack or global allocation.  If so, get the
  // allocated type.
  //
  Type * AllocatedType = 0;
  if (AllocaInst * AI = dyn_cast<AllocaInst>(Ptr->stripPointerCasts())) {
    if (!(AI->isArrayAllocation())) {
      AllocatedType = AI->getAllocatedType();
    }
  }

  if (GlobalVariable * GV=dyn_cast<GlobalVariable>(Ptr->stripPointerCasts())) {
    AllocatedType = GV->getType()->getElementType();
  }

  //
  // If this is not a stack or global object, it is unsafe (it might be
  // deallocated, for example).
  //
  if (!AllocatedType)
    return false;

  //
  // If the types are the same, then the access is safe.
  //
  if (AllocatedType == MemType)
    return true;

  //
  // Otherwise, see if the allocated type is larger than the accessed type.
  //
  TargetData & TD = getAnalysis<TargetData>();
  uint64_t AllocTypeSize = TD.getTypeAllocSize(AllocatedType);
  uint64_t MemTypeSize   = TD.getTypeStoreSize(MemType);
  return (AllocTypeSize >= MemTypeSize);
}

//
// Method: visitLoadInst()
//
// Description:
//  Place a run-time check on a load instruction.
//
void
InsertLSChecks::visitLoadInst (LoadInst & LI) {
  //
  // If the check will always succeed, skip it.
  //
  if (isTriviallySafe (LI.getPointerOperand(), LI.getType()))
    return;

  //
  // Create a value representing the amount of memory, in bytes, that will be
  // modified.
  //
  TargetData & TD = getAnalysis<TargetData>();
  uint64_t TypeSize=TD.getTypeStoreSize(LI.getType());
  IntegerType * IntType = IntegerType::getInt32Ty(LI.getContext());
  Value * AccessSize = ConstantInt::get (IntType, TypeSize);

  //
  // Create an STL container with the arguments.
  // The first argument is the pool handle (which is a NULL pointer).
  // The second argument is the pointer to check.
  //
  std::vector<Value *> args;
  LLVMContext & Context = LI.getContext();
  args.push_back(ConstantPointerNull::get (getVoidPtrType(Context)));
  args.push_back(castTo (LI.getPointerOperand(), getVoidPtrType(Context), &LI));
  args.push_back (AccessSize);

  //
  // Create the call to the run-time check.  Place it *before* the load
  // instruction.
  //
  CallInst * CI = CallInst::Create (PoolCheckUI, args, "", &LI);

  //
  // If there's debug information on the load instruction, add it to the
  // run-time check.
  //
  if (MDNode * MD = LI.getMetadata ("dbg"))
    CI->setMetadata ("dbg", MD);

  //
  // Update the statistics.
  //
  ++LSChecks;
  return;
}

//
// Method: visitStoreInst()
//
// Description:
//  Place a run-time check on a store instruction.
//
void
InsertLSChecks::visitStoreInst (StoreInst & SI) {
  //
  // If the check will always succeed, skip it.
  //
  if (isTriviallySafe (SI.getPointerOperand(), SI.getValueOperand()->getType()))
    return;

  //
  // Create a value representing the amount of memory, in bytes, that will be
  // modified.
  //
  TargetData & TD = getAnalysis<TargetData>();
  uint64_t TypeSize=TD.getTypeStoreSize(SI.getValueOperand()->getType());
  IntegerType * IntType = IntegerType::getInt32Ty(SI.getContext());
  Value * AccessSize = ConstantInt::get (IntType, TypeSize);

  //
  // Create an STL container with the arguments.
  // The first argument is the pool handle (which is a NULL pointer).
  // The second argument is the pointer to check.
  // The third argument is the amount of data that will be stored.
  //
  std::vector<Value *> args;
  LLVMContext & Context = SI.getContext();
  args.push_back(ConstantPointerNull::get (getVoidPtrType(Context)));
  args.push_back(castTo (SI.getPointerOperand(), getVoidPtrType(Context), &SI));
  args.push_back (AccessSize);

  //
  // Create the call to the run-time check.  Place it *before* the store
  // instruction.
  //
  CallInst * CI = CallInst::Create (PoolCheckUI, args, "", &SI);

  //
  // If there's debug information on the load instruction, add it to the
  // run-time check.
  //
  if (MDNode * MD = SI.getMetadata ("dbg"))
    CI->setMetadata ("dbg", MD);

  //
  // Update the statistics.
  //
  ++LSChecks;
  return;
}

void
InsertLSChecks::visitAtomicCmpXchgInst (AtomicCmpXchgInst & AI) {
  //
  // If the check will always succeed, skip it.
  //
  if (isTriviallySafe (AI.getPointerOperand(), AI.getType()))
    return;

  //
  // Create a value representing the amount of memory, in bytes, that will be
  // modified.
  //
  TargetData & TD = getAnalysis<TargetData>();
  LLVMContext & Context = AI.getContext();
  uint64_t TypeSize=TD.getTypeStoreSize(AI.getType());
  IntegerType * IntType = IntegerType::getInt32Ty (Context);
  Value * AccessSize = ConstantInt::get (IntType, TypeSize);

  //
  // Create an STL container with the arguments.
  // The first argument is the pool handle (which is a NULL pointer).
  // The second argument is the pointer to check.
  //
  std::vector<Value *> args;
  args.push_back(ConstantPointerNull::get (getVoidPtrType(Context)));
  args.push_back(castTo (AI.getPointerOperand(), getVoidPtrType(Context), &AI));
  args.push_back (AccessSize);

  //
  // Create the call to the run-time check.  Place it *before* the compare and
  // exchange instruction.
  //
  CallInst * CI = CallInst::Create (PoolCheckUI, args, "", &AI);

  //
  // If there's debug information on the load instruction, add it to the
  // run-time check.
  //
  if (MDNode * MD = AI.getMetadata ("dbg"))
    CI->setMetadata ("dbg", MD);

  //
  // Update the statistics.
  //
  ++LSChecks;
  return;
}

void
InsertLSChecks::visitAtomicRMWInst (AtomicRMWInst & AI) {
  //
  // If the check will always succeed, skip it.
  //
  if (isTriviallySafe (AI.getPointerOperand(), AI.getType()))
    return;

  //
  // Create a value representing the amount of memory, in bytes, that will be
  // modified.
  //
  TargetData & TD = getAnalysis<TargetData>();
  LLVMContext & Context = AI.getContext();
  uint64_t TypeSize=TD.getTypeStoreSize(AI.getType());
  IntegerType * IntType = IntegerType::getInt32Ty (Context);
  Value * AccessSize = ConstantInt::get (IntType, TypeSize);

  //
  // Create an STL container with the arguments.
  // The first argument is the pool handle (which is a NULL pointer).
  // The second argument is the pointer to check.
  //
  std::vector<Value *> args;
  args.push_back(ConstantPointerNull::get (getVoidPtrType(Context)));
  args.push_back(castTo (AI.getPointerOperand(), getVoidPtrType(Context), &AI));
  args.push_back (AccessSize);

  //
  // Create the call to the run-time check.  Place it *before* the compare and
  // exchange instruction.
  //
  CallInst * CI = CallInst::Create (PoolCheckUI, args, "", &AI);

  //
  // If there's debug information on the load instruction, add it to the
  // run-time check.
  //
  if (MDNode * MD = AI.getMetadata ("dbg"))
    CI->setMetadata ("dbg", MD);

  //
  // Update the statistics.
  //
  ++LSChecks;
  return;
}

//
// Method: doInitialization()
//
// Description:
//  Perform module-level initialization before the pass is run.  For this
//  pass, we need to create a function prototype for the load/store check
//  function.
//
// Inputs:
//  M - A reference to the LLVM module to modify.
//
// Return value:
//  true - This LLVM module has been modified.
//
bool
InsertLSChecks::doInitialization (Module & M) {
  //
  // Create a function prototype for the function that performs incomplete
  // load/store checks.
  //
  Type * VoidTy  = Type::getVoidTy (M.getContext());
  Type * VoidPtrTy = getVoidPtrType (M.getContext());
  Type * IntTy = IntegerType::getInt32Ty(M.getContext());
  Constant * F = M.getOrInsertFunction ("poolcheckui",
                                        VoidTy,
                                        VoidPtrTy,
                                        VoidPtrTy,
                                        IntTy,
                                        NULL);

  //
  // Mark the function as readonly; that will enable it to be hoisted out of
  // loops by the standard loop optimization passes.
  //
  (cast<Function>(F))->addFnAttr (Attribute::ReadOnly);
  return true;
}

bool
InsertLSChecks::runOnFunction (Function & F) {
  //
  // Get a pointer to the run-time check function.
  //
  PoolCheckUI = F.getParent()->getFunction ("poolcheckui");
  assert (PoolCheckUI && "Load/Store Check function has disappeared!\n");

  //
  // Visit all of the instructions in the function.
  //
  visit (F);
  return true;
}

}

