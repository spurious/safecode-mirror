//===- SCUtils.h - Utility Functions for SAFECode ----------------------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements several utility functions used by SAFECode.
//
//===----------------------------------------------------------------------===//

#ifndef _SCUTILS_H_
#define _SCUTILS_H_

#include "llvm/BasicBlock.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/LLVMContext.h"

#include <vector>
#include <set>
#include <string>

using namespace llvm;

namespace llvm {
//
// Function: isCheckingCall()
//
// Description:
//  Determine whether a function is a checking routine inserted by SafeCode.
//
// FIXME: currently the function stays in CodeDuplication.cpp, it
// should be a separate cpp file.
bool isCheckingCall(const std::string & functionName);

//
// Function: getVoidPtrType()
//
// Description:
//  Return a pointer to the LLVM type for a void pointer.
//
// Return value:
//  A pointer to an LLVM type for the void pointer.
//
// Notes:
//  This function cannot be used in a multi-threaded program because it uses
//  the LLVM Global Context.
//
//  Many, many passes create an LLVM void pointer type, and the code for it
//  takes up most of the 80 columns available in a line.  This function should
//  be easily inlined by the compiler and ease readability of the code (as well
//  as centralize changes when LLVM's Type API is changed).
//
static inline
PointerType * getVoidPtrType(void) {
  const Type * Int8Type  = IntegerType::getInt8Ty(getGlobalContext());
  return PointerType::getUnqual(Int8Type);
}

//
// Function: castTo()
//
// Description:
//  Given an LLVM value, insert a cast instruction to make it a given type.
//
static inline Value *
castTo (Value * V, const Type * Ty, Twine Name, Instruction * InsertPt) {
  //
  // Don't bother creating a cast if it's already the correct type.
  //
  assert (V && "castTo: trying to cast a NULL Value!\n");
  if (V->getType() == Ty)
    return V;
                                                                                
  //
  // If it's a constant, just create a constant expression.
  //
  if (Constant * C = dyn_cast<Constant>(V)) {
    Constant * CE = ConstantExpr::getZExtOrBitCast (C, Ty);
    return CE;
  }
                                                                                
  //
  // Otherwise, insert a cast instruction.
  //
  return CastInst::CreateZExtOrBitCast (V, Ty, Name, InsertPt);
}

static inline Instruction *
castTo (Instruction * I, const Type * Ty, Twine Name, Instruction * InsertPt) {
  //
  // Don't bother creating a cast if it's already the correct type.
  //
  assert (I && "castTo: trying to cast a NULL Instruction!\n");
  if (I->getType() == Ty)
    return I;
                                                                                
  //
  // Otherwise, insert a cast instruction.
  //
  return CastInst::CreateZExtOrBitCast (I, Ty, Name, InsertPt);
}

static inline Value *
castTo (Value * V, const Type * Ty, Instruction * InsertPt) {
  return castTo (V, Ty, "casted", InsertPt);
}

//
// Function: indexesStructsOnly()
//
// Description:
//  Determines whether the given GEP expression only indexes into structures.
//
// Return value:
//  true - This GEP only indexes into structures.
//  false - This GEP indexes into one or more arrays.
//
static inline bool
indexesStructsOnly (GetElementPtrInst * GEP) {
  const Type * PType = GEP->getPointerOperand()->getType();
  const Type * ElementType;
  unsigned int index = 1;
  std::vector<Value *> Indices;
  unsigned int maxOperands = GEP->getNumOperands() - 1;

  //
  // Check the first index of the GEP.  If it is non-zero, then it doesn't
  // matter what type we're indexing into; we're indexing into an array.
  //
  if (ConstantInt * CI = dyn_cast<ConstantInt>(GEP->getOperand(1)))
    if (!(CI->isNullValue ()))
      return false;

  //
  // Scan through all types except for the last.  If any of them are an array
  // type, the GEP is indexing into an array.
  //
  // If the last type is an array, the GEP returns a pointer to an array.  That
  // means the GEP itself is not indexing into the array; this is why we don't
  // check the type of the last GEP operand.
  //
  for (index = 1; index < maxOperands; ++index) {
    Indices.push_back (GEP->getOperand(index));
    ElementType = GetElementPtrInst::getIndexedType (PType,
                                                     Indices.begin(),
                                                     Indices.end());
    assert (ElementType && "ElementType is NULL!");
    if (isa<ArrayType>(ElementType)) {
      return false;
    }
  }

  return true;
}

//
// Function: peelCasts()
//
// Description:
//  This method peels off casts to get to the original instruction that
//  generated the value for the given instruction.
//
// Inputs:
//  PointerOperand - The value off of which we will peel the casts.
//
// Outputs:
//  Chain - The set of values that are between the original value and the
//          specified value.
//
// Return value:
//  A pointer to the LLVM value that originates the specified LLVM value.
//
static inline Value *
peelCasts (Value * PointerOperand, std::set<Value *> & Chain) {
  Value * SourcePointer = PointerOperand;
  bool done = false;

  while (!done) {
    //
    // Trace through constant cast and GEP expressions
    //
    if (ConstantExpr * cExpr = dyn_cast<ConstantExpr>(SourcePointer)) {
      if (cExpr->isCast()) {
        if (isa<PointerType>(cExpr->getOperand(0)->getType())) {
          Chain.insert (SourcePointer);
          SourcePointer = cExpr->getOperand(0);
          continue;
        }
      }

      // We cannot handle this expression; break out of the loop
      break;
    }

    //
    // Trace back through cast instructions.
    //
    if (CastInst * CastI = dyn_cast<CastInst>(SourcePointer)) {
      if (isa<PointerType>(CastI->getOperand(0)->getType())) {
        Chain.insert (SourcePointer);
        SourcePointer = CastI->getOperand(0);
        continue;
      }
      break;
    }

    // We can't scan through any more instructions; give up
    done = true;
  }

  return SourcePointer;
}

}

#endif

