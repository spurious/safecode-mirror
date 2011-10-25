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
// Method: visitLoadInst()
//
// Description:
//  Place a run-time check on a load instruction.
//
void
InsertLSChecks::visitLoadInst (LoadInst & LI) {
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
  M.getOrInsertFunction ("poolcheckui",
                         VoidTy,
                         VoidPtrTy,
                         VoidPtrTy,
                         IntTy,
                         NULL);
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

