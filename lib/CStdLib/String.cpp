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

static RegisterPass<StringTransform> X("string_transform",
                                       "Secure C standard string library calls");

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

  Function *F_strcpy = M.getFunction("strcpy");
  if (!F_strcpy)
    return modified;

  // Scan through the module and replace strcpy() with pool_strcpy().
  for (Value::use_iterator UI = F_strcpy->use_begin(), UE = F_strcpy->use_end(); UI != UE; ++UI) {
    if (Instruction *I = dyn_cast<Instruction>(UI)) {
      CallSite CS(I);
      Function *F = CS.getCalledFunction();

      if (NULL == F || F != F_strcpy)
        continue;

      if (CS.arg_size() != 2)
        std::cerr << *I << std::endl;
      assert(CS.arg_size() == 2 && "CStdLib (strcpyTransform): strcpy() takes 2 arguments!\n");

      PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
      Value *dstPH = dsnPass->getPoolHandle(I->getOperand(1), F, *FI);
      Value *srcPH = dsnPass->getPoolHandle(I->getOperand(2), F, *FI);

      if (!dstPH || !srcPH)
        std::cerr << *I << std::endl;
      assert(dstPH && "CStdLib (strcpyTransform): No pool handle for destination pointer!\n");
      assert(srcPH && "CStdLib (strcpyTransform): No pool handle for source pointer!\n");

      // Create the pool_strcpy() call.
      std::vector<const Type *> ParamTy;
      ParamTy.push_back(PointerType::getUnqual(Type::Int8Ty));
      ParamTy.push_back(PointerType::getUnqual(Type::Int8Ty));
      ParamTy.push_back(PointerType::getUnqual(Type::Int8Ty));
      ParamTy.push_back(PointerType::getUnqual(Type::Int8Ty));

      SmallVector<Value *, 4> Params;
      Params.push_back(dstPH);
      Params.push_back(srcPH);
      Params.push_back(I->getOperand(1));
      Params.push_back(I->getOperand(2));

      FunctionType * FT_strcpy = FunctionType::get(F_strcpy->getReturnType(), ParamTy, false);
      Constant * Callee = M.getOrInsertFunction("pool_strcpy", FT_strcpy);
      CallInst * CI_pool_strcpy = CallInst::Create(Callee, Params.begin(), Params.end());

      // Replace all of the uses of the strcpy() call with the new pool_strcpy() call.
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
