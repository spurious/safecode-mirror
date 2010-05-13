//===- ArrayBoundCheckStruct.cpp - Static Array Bounds Checking --------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements the ArrayBoundsCheckStruct pass.  This pass utilizes
// type-safety information from points-to analysis to prove whether GEPs are
// safe (they do not create a pointer outside of the memory object).  It is
// primarily designed to alleviate run-time checks on GEPs used for structure
// indexing (hence the clever name).
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "abc-struct"

#include "ArrayBoundsCheck.h"
#include "SCUtils.h"
#include "safecode/SAFECodeConfig.h"
#include "safecode/Support/AllocatorInfo.h"

#include "dsa/DSGraph.h"
#include "dsa/DSNode.h"

#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"

using namespace llvm;


namespace {
  STATISTIC (allGEPs ,  "Total Number of GEPs Queried");
  STATISTIC (safeGEPs , "Number of GEPs on Structures Proven Safe Statically");
}

NAMESPACE_SC_BEGIN

namespace {
  RegisterPass<ArrayBoundsCheckStruct>
  X ("abc-struct", "Structure Indexing Array Bounds Check pass");

  RegisterAnalysisGroup<ArrayBoundsCheckGroup> ABCGroup(X);
}

char ArrayBoundsCheckStruct::ID = 0;

//
// Method: runOnFunction()
//
// Description:
//  This is the entry point for this analysis pass.  We grab the required
//  analysis results from other passes here.  However, we don't actually
//  compute anything in this method.  Instead, we compute results when queried
//  by other passes.  This makes the bet that each GEP will only be quered
//  once, and only if some other analysis pass can't prove it safe before this
//  pass can.
//
// Inputs:
//  F - A reference to the function to analyze.
//
// Return value:
//  true  - This pass modified the function (which should never happen).
//  false - This pass did not modify the function.
//
bool
ArrayBoundsCheckStruct::runOnFunction(Function & F) {
  //
  // Get required analysis results from other passes.
  //
  abcPass = &getAnalysis<ArrayBoundsCheckGroup>();
  poolPass = &getAnalysis<QueryPoolPass>();

  //
  // We don't make any changes, so return false.
  //
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
ArrayBoundsCheckStruct::isGEPSafe (GetElementPtrInst * GEP) {
  //
  // Update the count of GEPs queried.
  //
  ++allGEPs;

  //
  // Get the source pointer of the GEP.  This is the pointer off of which the
  // indexing operation takes place.
  //
  Value * PointerOperand = GEP->getPointerOperand();

  //
  // Determine whether the pointer is for a type-known object.
  //
  if (poolPass->isTypeKnown (PointerOperand)) {
    //
    // The pointer points to a type-known object.  If the indices all index
    // into structures, then the GEP is safe.
    //
    if (indexesStructsOnly (GEP)) {
      ++safeGEPs;
      return true;
    }
  }

  //
  // We cannot statically prove that the GEP is safe.  Ask another array bounds
  // checking pass to prove the GEP safe.
  //
  return abcPass->isGEPSafe(GEP);
}


NAMESPACE_SC_END
