//===- LoadStoreChecks.cpp - Insert load/store run-time checks ------------ --//
// 
//                          The SAFECode Compiler 
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

#include "safecode/SAFECode.h"
#include "safecode/LoadStoreChecks.h"
#include "SCUtils.h"

#include "llvm/ADT/Statistic.h"

NAMESPACE_SC_BEGIN

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
  // Create an STL container with the arguments.
  // The first argument is the pool handle (which is a NULL pointer).
  // The second argument is the pointer to check.
  //
  std::vector<Value *> args;
  LLVMContext & Context = LI.getContext();
  args.push_back(ConstantPointerNull::get (getVoidPtrType(Context)));
  args.push_back(castTo (LI.getPointerOperand(), getVoidPtrType(Context), &LI));

  //
  // Create the call to the run-time check.  Place it *before* the load
  // instruction.
  //
  CallInst::Create (PoolCheckUI, args.begin(), args.end(), "", &LI);

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
  // Create an STL container with the arguments.
  // The first argument is the pool handle (which is a NULL pointer).
  // The second argument is the pointer to check.
  //
  std::vector<Value *> args;
  LLVMContext & Context = SI.getContext();
  args.push_back(ConstantPointerNull::get (getVoidPtrType(Context)));
  args.push_back(castTo (SI.getPointerOperand(), getVoidPtrType(Context), &SI));

  //
  // Create the call to the run-time check.  Place it *before* the store
  // instruction.
  //
  CallInst::Create (PoolCheckUI, args.begin(), args.end(), "", &SI);

  //
  // Update the statistics.
  //
  ++LSChecks;
  return;
}

bool
InsertLSChecks::runOnFunction (Function & F) {
  //
  // Get pointers to required analysis passes.
  //
  TD      = &getAnalysis<TargetData>();

  //
  // Get a pointer to the run-time check function.
  //
  PoolCheckUI = F.getParent()->getFunction ("sc.lscheckui");

  //
  // Visit all of the instructions in the function.
  //
  visit (F);
  return true;
}

NAMESPACE_SC_END

