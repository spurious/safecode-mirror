//===-------- String.cpp - Secure C standard string library calls ---------===//
//
//                          The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass finds all calls to functions in the C standard string library and
// transforms them to a more secure form.
//
//===----------------------------------------------------------------------===//

#include "safecode/CStdLib.h"

NAMESPACE_SC_BEGIN

// Identifier variable for the pass
char StringTransform::ID = 0;

// Statistics counters
STATISTIC(stat_transform_strcpy, "Total strcpy() calls transformed");

static RegisterPass<StringTransform>
ST("string_transform", "Secure C standard string library calls");

/**
 * Entry point for the LLVM pass that transforms C standard string library calls
 *
 * @param	M	Module to scan
 * @return	Whether we modified the module
 */
bool StringTransform::runOnModule(Module &M) {
  // Flags whether we modified the module
  bool modified = false;

  dsnPass = &getAnalysis<DSNodePass>();
  assert(dsnPass && "Must run DSNode Pass first!");

  paPass = &getAnalysis<PoolAllocateGroup>();
  assert(paPass && "Must run Pool Allocation Transform first!");

  modified |= strcpyTransform(M);

  return modified;
}

/**
 * Secures strcpy() by transforming it into pool_strcpy() with correct bounds
 *
 * @param	M	Module from runOnModule() to scan for strcpy()
 * @return	Whether we modified the module
 */
bool StringTransform::strcpyTransform(Module &M) {
  // Flags whether we modified the module
  bool modified = false;

  //
  // Create needed pointer types.
  //
  const Type * Int8Type  = IntegerType::getInt8Ty(getGlobalContext());
  PointerType *VoidPtrTy = PointerType::getUnqual(Int8Type);

  Function *F_strcpy = M.getFunction("strcpy");
  if (!F_strcpy)
    return modified;

  // Scan through the module and replace strcpy() with pool_strcpy().
  for (Value::use_iterator UI = F_strcpy->use_begin(), UE = F_strcpy->use_end(); UI != UE;) {
    Instruction *I = dyn_cast<Instruction>(UI);
    ++UI; // Replacement invalidates the user, so must increment the iterator beforehand.

    if (I) {
      CallSite CS(I);
      Function *CalledF = CS.getCalledFunction();

      // Indirect call.
      if (NULL == CalledF)
        continue;

      if (F_strcpy != CalledF)
        continue;

      // Check that the function uses the correct number of arguments.
      if (CS.arg_size() != 2) {
        std::cerr << *I << std::endl;
        assert(CS.arg_size() == 2 && "strcpy() takes 2 arguments!\n");
        continue;
      }

      // Check for correct return type (char * == i8 * == VoidPtrTy).
      if (CalledF->getReturnType() != VoidPtrTy)
        continue;

      Function * F = I->getParent()->getParent();
      PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
      Value *dstPH = dsnPass->getPoolHandle(I->getOperand(1), F, *FI);
      Value *srcPH = dsnPass->getPoolHandle(I->getOperand(2), F, *FI);

      if (!dstPH || !srcPH)
        std::cerr << *I << std::endl;
      assert(dstPH && "No pool handle for destination pointer!\n");
      assert(srcPH && "No pool handle for source pointer!\n");

      Value *Casted_dstPH = castTo(dstPH, VoidPtrTy, dstPH->getName() + ".casted", I);
      Value *Casted_srcPH = castTo(srcPH, VoidPtrTy, srcPH->getName() + ".casted", I);

      // Construct pool_strcpy().
      std::vector<const Type *> ParamTy(4, VoidPtrTy); // SmallVector<const Type *, 4>
      Value *Params[] = {Casted_dstPH, Casted_srcPH, I->getOperand(1), I->getOperand(2)};
      FunctionType *FT_pool_strcpy = FunctionType::get(F_strcpy->getReturnType(), ParamTy, false);
      Constant *F_pool_strcpy = M.getOrInsertFunction("pool_strcpy", FT_pool_strcpy);

      // Create the call instruction for pool_strcpy() and insert it before the current instruction.
      CallInst *CI_pool_strcpy = CallInst::Create(F_pool_strcpy, Params, array_endof(Params), "", I);
      
      // Replace all of the uses of strcpy() with pool_strcpy().
      I->replaceAllUsesWith(CI_pool_strcpy);
      I->eraseFromParent();

      // Record the transform.
      ++stat_transform_strcpy;

      // Mark the module as modified and continue to the next strcpy() call.
      modified = true;
    }
  }

  return modified;
}

NAMESPACE_SC_END
