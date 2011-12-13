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

#include <vector>

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

bool
InitAllocas::doInitialization (Module & M) {
  //
  // Create needed LLVM types.
  //
  Type * VoidType  = Type::getVoidTy(M.getContext());
  Type * Int1Type  = IntegerType::getInt1Ty(M.getContext());
  Type * Int8Type  = IntegerType::getInt8Ty(M.getContext());
  Type * Int32Type = IntegerType::getInt32Ty(M.getContext());
  Type * VoidPtrType = PointerType::getUnqual(Int8Type);

  //
  // Add the memset function to the program.
  //
  M.getOrInsertFunction ("llvm.memset.p0i8.i32",
                         VoidType,
                         VoidPtrType,
                         Int8Type,
                         Int32Type,
                         Int32Type,
                         Int1Type,
                         NULL);

  return true;
}

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
  // Scan for a place to insert the instruction to initialize the
  // allocated memory.
  //
  Instruction * InsertPt = getInsertionPoint (AI);

  //
  // If the alloca allocates an array of significant size, use a memset of
  // initialize it.  The LLVM code generators can assert out with
  // zeroinitializers of large aggregate size.
  //
  bool useMemset = false;
  Type * AllocType = AI.getAllocatedType();
  if ((isa<ArrayType>(AllocType)) || (isa<VectorType>(AllocType))) {
    useMemset = true;
  }

  if (useMemset) {
    //
    // Get access to the pass that tells us how large types are.
    //
    TargetData & TD = getAnalysis<TargetData>();

    //
    // Get various types that we'll need.
    //
    Type * Int1Type    = IntegerType::getInt1Ty(AI.getContext());
    Type * Int8Type    = IntegerType::getInt8Ty(AI.getContext());
    Type * Int32Type   = IntegerType::getInt32Ty(AI.getContext());
    Type * VoidPtrType = getVoidPtrType (AI.getContext());

    //
    // Create a call to memset.
    //
    Module * M = AI.getParent()->getParent()->getParent();
    Function * Memset = cast<Function>(M->getFunction ("llvm.memset.p0i8.i32"));
    std::vector<Value *> args;
    args.push_back (castTo (&AI, VoidPtrType, AI.getName().str(), InsertPt));
    args.push_back (ConstantInt::get(Int8Type, 0));
    args.push_back (ConstantInt::get(Int32Type,TD.getTypeAllocSize(AllocType)));
    args.push_back (ConstantInt::get(Int32Type, 0));
    args.push_back (ConstantInt::get(Int1Type, 0));
    CallInst::Create (Memset, args, "", InsertPt);
  } else {
    //
    // Create an aggregate zero value to initialize the alloca.
    //
    Constant * Init = Constant::getNullValue (AI.getAllocatedType());

    //
    // Store the zero value into the allocated memory.
    //
    new StoreInst (Init, &AI, InsertPt);
  }

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

