//===- GEPChecks.cpp - Insert GEP run-time checks ------------------------- --//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass instruments GEPs with run-time checks to ensure safe array and
// structure indexing.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "safecode"

#include "safecode/SAFECode.h"
#include "safecode/InsertChecks.h"
#include "SCUtils.h"

#include "llvm/ADT/Statistic.h"

NAMESPACE_SC_BEGIN

char InsertGEPChecks::ID = 0;

static RegisterPass<InsertGEPChecks>
X ("gepchecks", "Insert GEP run-time checks");

//
// Command Line Options
//

// Disable checks on pure structure indexing
cl::opt<bool> DisableStructChecks ("disable-structgepchecks", cl::Hidden,
                                   cl::init(false),
                                   cl::desc("Disable Struct GEP Checks"));

// Pass Statistics
namespace {
  STATISTIC (GEPChecks, "Bounds Checks Added");
  STATISTIC (SafeGEP,   "GEPs proven safe by SAFECode");
}

//
// Method: visitGetElementPtrInst()
//
// Description:
//  This method checks to see if the specified GEP is safe.  If it cannot prove
//  it safe, it then adds a run-time check for it.
//
void
InsertGEPChecks::visitGetElementPtrInst (GetElementPtrInst & GEP) {
  //
  // Determine if the GEP is safe.  If it is, then don't do anything.
  //
  if (abcPass->isGEPSafe(&GEP)) {
    ++SafeGEP;
    return;
  }

  //
  // If this only indexes into a structure, ignore it.
  //
  if (DisableStructChecks && indexesStructsOnly (&GEP)) {
    return;
  }

  //
  // Get the function in which the GEP instruction lives.
  //
  Value * PH = ConstantPointerNull::get (getVoidPtrType());
  BasicBlock::iterator InsertPt = &GEP;
  ++InsertPt;
  Instruction * ResultPtr = castTo (&GEP,
                                    getVoidPtrType(),
                                    GEP.getName() + ".cast",
                                    InsertPt);

  //
  // Make this an actual cast instruction; it will make it easier to update
  // DSA.
  //
  Value * SrcPtr = castTo (GEP.getPointerOperand(),
                           getVoidPtrType(),
                           GEP.getName()+".cast",
                           InsertPt);

  //
  // Create the call to the run-time check.
  //
  std::vector<Value *> args(1, PH);
  args.push_back (SrcPtr);
  args.push_back (ResultPtr);
  CallInst::Create (PoolCheckArrayUI, args.begin(), args.end(), "", InsertPt);

  //
  // Update the statistics.
  //
  ++GEPChecks;
  return;
}

bool
InsertGEPChecks::runOnFunction (Function & F) {
  //
  // Get pointers to required analysis passes.
  //
  TD      = &getAnalysis<TargetData>();
  abcPass = &getAnalysis<ArrayBoundsCheckGroup>();

  //
  // Get a pointer to the run-time check function.
  //
  PoolCheckArrayUI 	= F.getParent()->getFunction ("sc.boundscheckui");

  //
  // Visit all of the instructions in the function.
  //
  visit (F);
  return true;
}

NAMESPACE_SC_END

