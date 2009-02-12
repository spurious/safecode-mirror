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


#include "llvm/BasicBlock.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"

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
// Function: castTo()
//
// Description:
//  Given an LLVM value, insert a cast instruction to make it a given type.
//
static inline Value *
castTo (Value * V, const Type * Ty, std::string Name, Instruction * InsertPt) {
  //
  // Don't bother creating a cast if it's already the correct type.
  //
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
castTo (Instruction * I, const Type * Ty, std::string Name,
        Instruction * InsertPt) {
  //
  // Don't bother creating a cast if it's already the correct type.
  //
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
#if 0
  const Type * PType = GEP->getPointerOperand()->getType();
  const Type * ElementType;
#endif
  unsigned int index = 1;
  std::vector<Value *> Indices;
#if 0
  unsigned int maxOperands = GEP->getNumOperands() - 1;
#else
  unsigned int maxOperands = GEP->getNumOperands();
#endif

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
#if 0
    Indices.push_back (GEP->getOperand(index));
    ElementType=GetElementPtrInst::getIndexedType (PType, Indices, true);
    assert (ElementType && "ElementType is NULL!");
    if (isa<ArrayType>(ElementType))
      return false;
#else
    if (!(isa<ConstantInt>(GEP->getOperand(index))))
      return false;
#endif
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
