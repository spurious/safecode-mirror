//===- ArrayBoundCheck.cpp - Static Array Bounds Checking --------------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// ArrayBoundsCheckLocal - It tries to prove a GEP is safe only based on local
// information, that is, the size of global variables and the size of objects
// being allocated inside a function.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "abc"

#include "ArrayBoundsCheck.h"
#include "safecode/SAFECodeConfig.h"
#include "safecode/Support/AllocatorInfo.h"

using namespace llvm;


NAMESPACE_SC_BEGIN

namespace {
  RegisterPass<ArrayBoundsCheckLocal> X ("abc-local", "Local Array Bounds Check pass");
  RegisterAnalysisGroup<ArrayBoundsCheckGroup> ABCGroup(X);
}

char ArrayBoundsCheckLocal::ID = 0;

bool
ArrayBoundsCheckLocal::runOnFunction(Function & F) {
  intrinsicPass = &getAnalysis<InsertSCIntrinsic>();
  TD = &getAnalysis<TargetData>();
  return false;
}

// Determine whether the indices of the GEP are all constant.
bool
ArrayBoundsCheckLocal::isConstantIndexGEP(GetElementPtrInst * GEP) {
  for (unsigned index = 1; index < GEP->getNumOperands(); ++index) {
    if (ConstantInt * CI = dyn_cast<ConstantInt>(GEP->getOperand(index))) {
      if (CI->getSExtValue() < 0) {
        return false;
      }
    } else {
      return false;
    }
  }
  return true;
}

//
// Function: isGEPSafe()
//
// Description:
//  Determine whether the GEP will always generate a pointer that lands within
//  the bounds of the object.
//
// Inputs:
//  TD  - The TargetData pass which should be used for finding type-sizes and
//        offsets of elements within a derived type.
//  GEP - The getelementptr instruction to check.
//
// Return value:
//  true  - The GEP never generates a pointer outside the bounds of the object.
//  false - The GEP may generate a pointer outside the bounds of the object.
//          There may also be cases where we know that the GEP *will* return an
//          out-of-bounds pointer; we let pointer rewriting take care of those
//          cases.
//
bool
ArrayBoundsCheckLocal::isGEPSafe (GetElementPtrInst * GEP) {
  if (!isConstantIndexGEP(GEP))
    return false;

  //
  // Check to see if we're indexing off the beginning of a known object.  If
  // so, then find the size of the object.  Otherwise, assume the size is zero.
  //
  Value * PointerOperand = GEP->getPointerOperand();
  Value * TypeSize = intrinsicPass->getObjectSize(PointerOperand);
  unsigned int type_size = 0;
  if (TypeSize && isa<ConstantInt>(TypeSize)) {
    ConstantInt * C = cast<ConstantInt>(TypeSize);
    type_size = C->getSExtValue();
  }
  
  //
  // If the type size is non-zero, then we did, in fact, find an object off of
  // which the GEP is indexing.  Statically determine if the indexing operation
  // is always within bounds.
  //
  if (type_size > 0) {
    Value ** Indices  = new Value *[GEP->getNumOperands() - 1];

    for (unsigned index = 1; index < GEP->getNumOperands(); ++index) {
      Indices[index - 1] = GEP->getOperand(index);
    }

    unsigned offset = TD->getIndexedOffset (PointerOperand->getType(),
                                            Indices,
                                            GEP->getNumOperands() - 1);

    delete[] Indices;

    if (offset < type_size) {
      return true;
    }
  }

  //
  // We cannot statically prove that the GEP is safe.
  //
  return false;
}


NAMESPACE_SC_END
