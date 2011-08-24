//===- InitAllocas.cpp - Initialize allocas with pointers -------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a pass that ensures that uninitialized memory created
// by alloca instructions is not used to violate memory safety.  It can do this
// in one of two ways:
//
//   o) Promote the allocations from stack to heap.
//   o) Insert code to initialize the newly allocated memory.
//
// The current implementation implements the latter, but code for the former is
// available but disabled.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "init-allocas"

#include "safecode/InitAllocas.h"
#include "safecode/Utility.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/Pass.h"
#include "llvm/BasicBlock.h"
#include "llvm/Type.h"
#include "llvm/Function.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Support/Debug.h"

#include <iostream>

using namespace llvm;

char llvm::InitAllocas::ID = 0;

static RegisterPass<InitAllocas>
Z ("initallocas", "Initialize stack allocations containing pointers");

namespace {
  STATISTIC (InitedAllocas, "Allocas Initialized");
}

//
// Function: getInsertionPoint()
//
// Description:
//  Given an alloca instruction, skip past all subsequent alloca instructions
//  to find an ideal insertion point for instrumenting the alloca.
//
static inline Instruction *
getInsertionPoint (AllocaInst & AI) {
  //
  // Start with the instruction immediently after the alloca.
  //
  BasicBlock::iterator InsertPt = &AI;
  ++InsertPt;

  //
  // Keep skipping over instructions while they are allocas.
  //
  while (isa<AllocaInst>(InsertPt))
    ++InsertPt;
  return InsertPt;
}

namespace llvm {

//
// Method: visitAllocaInst()
//
// Description:
//  This method instruments an alloca instruction so that it is zero'ed out
//  before any data is loaded from it.
//
void
InitAllocas::visitAllocaInst (AllocaInst & AI) {
  //
  // Do not generate excessively large stores.
  //
  if (ArrayType * AT = dyn_cast<ArrayType>(AI.getAllocatedType())) {
    if (AT->getNumElements() > 10000)
      return;
  }

  //
  // Create an aggregate zero value to initialize the alloca.
  //
  Constant * Init = Constant::getNullValue (AI.getAllocatedType());

  //
  // Scan for a place to insert the instruction to initialize the
  // allocated memory.
  //
  Instruction * InsertPt = getInsertionPoint (AI);

  //
  // Store the zero value into the allocated memory.
  //
  new StoreInst (Init, &AI, InsertPt);

  //
  // Update statistics.
  //
  ++InitedAllocas;
  return;
}

bool
InitAllocas::runOnFunction (Function &F) {
  // Don't bother processing external functions
  if (F.isDeclaration())
    return false;

  visit (F);
  return true;
}

}

