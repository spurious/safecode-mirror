#include "llvm/BasicBlock.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"

#include <vector>

using namespace llvm;

//
// Function: getNextInst()
//
// Description:
//  Get the next instruction following this instruction.
//
// Return value:
//  0 - There is no instruction after this instruction in the Basic Block.
//  Otherwise, a pointer to the next instruction is returned.
//
Instruction *
getNextInst (Instruction * Inst) {
  BasicBlock::iterator i(Inst);
  ++i;
  if ((i) == Inst->getParent()->getInstList().end())
    return 0;
  return i;
}

#if 0
//
// Function: castTo()
//
// Description:
//  Given an LLVM value, insert a cast instruction to make it a given type.
//
static inline Value *
castTo (Value * V, const Type * Ty, Instruction * InsertPt) {
  //
  // Don't bother creating a cast if it's already the correct type.
  //
  if (V->getType() == Ty)
    return V;
                                                                                
  //
  // If it's a constant, just create a constant expression.
  //
  if (Constant * C = dyn_cast<Constant>(V)) {
    Constant * CE = ConstantExpr::getCast (C, Ty);
    return CE;
  }
                                                                                
  //
  // Otherwise, insert a cast instruction.
  //
  return new CastInst(V, Ty, "cast", InsertPt);
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
#endif

