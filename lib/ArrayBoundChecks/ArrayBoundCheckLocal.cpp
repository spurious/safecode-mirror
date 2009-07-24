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

#define DEBUG_TYPE "abc-local"

#include "ArrayBoundsCheck.h"
#include "safecode/SAFECodeConfig.h"
#include "safecode/Support/AllocatorInfo.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

using namespace llvm;


namespace {
  STATISTIC (allGEPs ,  "Total Number of GEPs Queried");
  STATISTIC (safeGEPs , "Number of GEPs Proven Safe Statically");
}

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
  SE = &getAnalysis<ScalarEvolution>();
  return false;
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
  //
  // Update the count of GEPs queried.
  //
  ++allGEPs;

  Value * PointerOperand = GEP->getPointerOperand();

  Value * objSize = intrinsicPass->getObjectSize(PointerOperand);
  if (!objSize)
    return false;

  const SCEV * GEPBase = SE->getSCEV(PointerOperand);
  const SCEV * offset = SE->getMinusSCEV(SE->getSCEV(GEP), GEPBase);
  const SCEV * bounds = SE->getSCEV(objSize);
  const SCEV * diff = SE->getMinusSCEV(bounds, offset);
  const SCEV * zero = SE->getSCEV(getGlobalContext().getConstantAggregateZero(Type::Int32Ty));

  if (SE->getSMaxExpr(diff, zero) == diff) {
    ++safeGEPs;
    return true;
  }
  
  //
  // We cannot statically prove that the GEP is safe.
  //
  return false;
}


NAMESPACE_SC_END
