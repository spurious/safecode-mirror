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
STATISTIC(stat_transform_memcpy, "Total memcpy() calls transformed");
STATISTIC(stat_transform_memmove, "Total memmove() calls transformed");
STATISTIC(stat_transform_mempcpy, "Total mempcpy() calls transformed");
STATISTIC(stat_transform_memset, "Total memset() calls transformed");
STATISTIC(stat_transform_strcat, "Total strcat() calls transformed");
STATISTIC(stat_transform_strcpy, "Total strcpy() calls transformed");
STATISTIC(stat_transform_strlcat, "Total strlcat() calls transformed");
STATISTIC(stat_transform_strlcpy, "Total strlcpy() calls transformed");
STATISTIC(stat_transform_strlen, "Total strlen() calls transformed");
STATISTIC(stat_transform_strncat, "Total strncat() calls transformed");
STATISTIC(stat_transform_strncpy, "Total strncpy() calls transformed");
STATISTIC(stat_transform_strnlen, "Total strnlen() calls transformed");
STATISTIC(stat_transform_wcscpy, "Total wcscpy() calls transformed");
STATISTIC(stat_transform_wmemcpy, "Total wmemcpy() calls transformed");
STATISTIC(stat_transform_wmemmove, "Total wmemmove() calls transformed");

static RegisterPass<StringTransform>
ST("string_transform", "Secure C standard string library calls");

/**
 * Entry point for the LLVM pass that transforms C standard string library calls
 *
 * @param	M	Module to scan
 * @return	Whether we modified the module
 */
bool StringTransform::runOnModule(Module &M) {
  // Flags whether we modified the module.
  bool modified = false;

  dsnPass = &getAnalysis<DSNodePass>();
  assert(dsnPass && "Must run DSNode Pass first!");

  paPass = &getAnalysis<PoolAllocateGroup>();
  assert(paPass && "Must run Pool Allocation Transform first!");

  modified |= memcpyTransform(M);
  modified |= memmoveTransform(M);
  modified |= mempcpyTransform(M);
  modified |= memsetTransform(M);
  modified |= strcpyTransform(M);
  modified |= strlenTransform(M);
  modified |= strncpyTransform(M);

  return modified;
}

/**
 * Secures memcpy() by transforming it into pool_memcpy() with correct bounds
 *
 * @param	M	Module from runOnModule() to scan for memcpy()
 * @return	Whether we modified the module
 */
bool StringTransform::memcpyTransform(Module &M) {
  // Flag whether we modified the module.
  bool modified = false;

  // Create needed pointer types.
  const Type *Int8Ty = IntegerType::getInt8Ty(getGlobalContext());
  PointerType *VoidPtrTy = PointerType::getUnqual(Int8Ty);

  Function *F_memcpy = M.getFunction("memcpy");
  if (!F_memcpy)
    return modified;

  // Scan through the module and replace memcpy() with pool_memcpy().
  for (Value::use_iterator UI = F_memcpy->use_begin(), UE = F_memcpy->use_end(); UI != UE;) {
    Instruction *I = dyn_cast<Instruction>(UI);
    ++UI; // Replacement invalidates the user, so must increment the iterator beforehand.

    if (I) {
      CallSite CS(I);
      Function *CalledF = CS.getCalledFunction();

      // Indirect call.
      if (NULL == CalledF)
        continue;

      if (F_memcpy != CalledF)
        continue;

      // Check that the function uses the correct number of arguments.
      if (CS.arg_size() != 3) {
        std::cerr << *I << std::endl;
        assert(CS.arg_size() == 3 && "memcpy() takes 3 arguments!\n");
        continue;
      }

      // Check for correct return type (void * == VoidPtrTy).
      if (CalledF->getReturnType() != VoidPtrTy)
        continue;

      // Get the exact type for size_t.
      const Type *SizeTy = I->getOperand(3)->getType();

      // Get pool handles for destination and source strings.
      Function *F = I->getParent()->getParent();
      PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
      Value *dstPH = dsnPass->getPoolHandle(I->getOperand(1), F, *FI);
      Value *srcPH = dsnPass->getPoolHandle(I->getOperand(2), F, *FI);

      if (!dstPH || !srcPH)
        std::cerr << *I << std::endl;
      assert(dstPH && "No pool handle for destination pointer!\n");
      assert(srcPH && "No pool handle for source pointer!\n");

      Value *Casted_dstPH = castTo(dstPH, VoidPtrTy, dstPH->getName() + ".casted", I);
      Value *Casted_srcPH = castTo(srcPH, VoidPtrTy, srcPH->getName() + ".casted", I);

      // Construct pool_memcpy().
      std::vector<const Type *> ParamTy(4, VoidPtrTy);
      ParamTy.push_back(SizeTy);
      Value *Params[] = {Casted_dstPH, Casted_srcPH, I->getOperand(1), I->getOperand(2), I->getOperand(3)};
      FunctionType *FT_pool_memcpy = FunctionType::get(F_memcpy->getReturnType(), ParamTy, false);
      Constant *F_pool_memcpy = M.getOrInsertFunction("pool_memcpy", FT_pool_memcpy);

      // Create the call instruction for pool_memcpy() and insert it before the current instruction.
      CallInst *CI_pool_memcpy = CallInst::Create(F_pool_memcpy, Params, array_endof(Params), "", I);
      
      // Replace all of the uses of memcpy() with pool_memcpy().
      I->replaceAllUsesWith(CI_pool_memcpy);
      I->eraseFromParent();

      // Record the transform.
      ++stat_transform_memcpy;

      // Mark the module as modified and continue to the next memcpy() call.
      modified = true;
    }
  }

  return modified;
}

/**
 * Secures memmove() by transforming it into pool_memmove() with correct bounds
 *
 * @param	M	Module from runOnModule() to scan for memmove()
 * @return	Whether we modified the module
 */
bool StringTransform::memmoveTransform(Module &M) {
  // Flag whether we modified the module.
  bool modified = false;

  // Create needed pointer types.
  const Type *Int8Ty = IntegerType::getInt8Ty(getGlobalContext());
  PointerType *VoidPtrTy = PointerType::getUnqual(Int8Ty);

  Function *F_memmove = M.getFunction("memmove");
  if (!F_memmove)
    return modified;

  // Scan through the module and replace memmove() with pool_memmove().
  for (Value::use_iterator UI = F_memmove->use_begin(), UE = F_memmove->use_end(); UI != UE;) {
    Instruction *I = dyn_cast<Instruction>(UI);
    ++UI; // Replacement invalidates the user, so must increment the iterator beforehand.

    if (I) {
      CallSite CS(I);
      Function *CalledF = CS.getCalledFunction();

      // Indirect call.
      if (NULL == CalledF)
        continue;

      if (F_memmove != CalledF)
        continue;

      // Check that the function uses the correct number of arguments.
      if (CS.arg_size() != 3) {
        std::cerr << *I << std::endl;
        assert(CS.arg_size() == 3 && "memmove() takes 3 arguments!\n");
        continue;
      }

      // Check for correct return type (void * == VoidPtrTy).
      if (CalledF->getReturnType() != VoidPtrTy)
        continue;

      // Get the exact type for size_t.
      const Type *SizeTy = I->getOperand(3)->getType();

      // Get pool handles for destination and source strings.
      Function *F = I->getParent()->getParent();
      PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
      Value *dstPH = dsnPass->getPoolHandle(I->getOperand(1), F, *FI);
      Value *srcPH = dsnPass->getPoolHandle(I->getOperand(2), F, *FI);

      if (!dstPH || !srcPH)
        std::cerr << *I << std::endl;
      assert(dstPH && "No pool handle for destination pointer!\n");
      assert(srcPH && "No pool handle for source pointer!\n");

      Value *Casted_dstPH = castTo(dstPH, VoidPtrTy, dstPH->getName() + ".casted", I);
      Value *Casted_srcPH = castTo(srcPH, VoidPtrTy, srcPH->getName() + ".casted", I);

      // Construct pool_memmove().
      std::vector<const Type *> ParamTy(4, VoidPtrTy);
      ParamTy.push_back(SizeTy);
      Value *Params[] = {Casted_dstPH, Casted_srcPH, I->getOperand(1), I->getOperand(2), I->getOperand(3)};
      FunctionType *FT_pool_memmove = FunctionType::get(F_memmove->getReturnType(), ParamTy, false);
      Constant *F_pool_memmove = M.getOrInsertFunction("pool_memmove", FT_pool_memmove);

      // Create the call instruction for pool_memmove() and insert it before the current instruction.
      CallInst *CI_pool_memmove = CallInst::Create(F_pool_memmove, Params, array_endof(Params), "", I);
      
      // Replace all of the uses of memmove() with pool_memmove().
      I->replaceAllUsesWith(CI_pool_memmove);
      I->eraseFromParent();

      // Record the transform.
      ++stat_transform_memmove;

      // Mark the module as modified and continue to the next memmove() call.
      modified = true;
    }
  }

  return modified;
}

/**
 * Secures mempcpy() by transforming it into pool_mempcpy() with correct bounds
 *
 * @param	M	Module from runOnModule() to scan for mempcpy()
 * @return	Whether we modified the module
 */
bool StringTransform::mempcpyTransform(Module &M) {
  // Flag whether we modified the module.
  bool modified = false;

  // Create needed pointer types.
  const Type *Int8Ty = IntegerType::getInt8Ty(getGlobalContext());
  PointerType *VoidPtrTy = PointerType::getUnqual(Int8Ty);

  Function *F_mempcpy = M.getFunction("mempcpy");
  if (!F_mempcpy)
    return modified;

  // Scan through the module and replace mempcpy() with pool_mempcpy().
  for (Value::use_iterator UI = F_mempcpy->use_begin(), UE = F_mempcpy->use_end(); UI != UE;) {
    Instruction *I = dyn_cast<Instruction>(UI);
    ++UI; // Replacement invalidates the user, so must increment the iterator beforehand.

    if (I) {
      CallSite CS(I);
      Function *CalledF = CS.getCalledFunction();

      // Indirect call.
      if (NULL == CalledF)
        continue;

      if (F_mempcpy != CalledF)
        continue;

      // Check that the function uses the correct number of arguments.
      if (CS.arg_size() != 3) {
        std::cerr << *I << std::endl;
        assert(CS.arg_size() == 3 && "mempcpy() takes 3 arguments!\n");
        continue;
      }

      // Check for correct return type (void * == VoidPtrTy).
      if (CalledF->getReturnType() != VoidPtrTy)
        continue;

      // Get the exact type for size_t.
      const Type *SizeTy = I->getOperand(3)->getType();

      // Get pool handles for destination and source strings.
      Function *F = I->getParent()->getParent();
      PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
      Value *dstPH = dsnPass->getPoolHandle(I->getOperand(1), F, *FI);
      Value *srcPH = dsnPass->getPoolHandle(I->getOperand(2), F, *FI);

      if (!dstPH || !srcPH)
        std::cerr << *I << std::endl;
      assert(dstPH && "No pool handle for destination pointer!\n");
      assert(srcPH && "No pool handle for source pointer!\n");

      Value *Casted_dstPH = castTo(dstPH, VoidPtrTy, dstPH->getName() + ".casted", I);
      Value *Casted_srcPH = castTo(srcPH, VoidPtrTy, srcPH->getName() + ".casted", I);

      // Construct pool_mempcpy().
      std::vector<const Type *> ParamTy(4, VoidPtrTy);
      ParamTy.push_back(SizeTy);
      Value *Params[] = {Casted_dstPH, Casted_srcPH, I->getOperand(1), I->getOperand(2), I->getOperand(3)};
      FunctionType *FT_pool_mempcpy = FunctionType::get(F_mempcpy->getReturnType(), ParamTy, false);
      Constant *F_pool_mempcpy = M.getOrInsertFunction("pool_mempcpy", FT_pool_mempcpy);

      // Create the call instruction for pool_mempcpy() and insert it before the current instruction.
      CallInst *CI_pool_mempcpy = CallInst::Create(F_pool_mempcpy, Params, array_endof(Params), "", I);
      
      // Replace all of the uses of mempcpy() with pool_mempcpy().
      I->replaceAllUsesWith(CI_pool_mempcpy);
      I->eraseFromParent();

      // Record the transform.
      ++stat_transform_mempcpy;

      // Mark the module as modified and continue to the next mempcpy() call.
      modified = true;
    }
  }

  return modified;
}

/**
 * Secures memset() by transforming it into pool_memset() with correct bounds
 *
 * @param	M	Module from runOnModule() to scan for memset()
 * @return	Whether we modified the module
 */
bool StringTransform::memsetTransform(Module &M) {
  // Flag whether we modified the module.
  bool modified = false;

  // Create needed pointer types.
  const Type *Int8Ty = IntegerType::getInt8Ty(getGlobalContext());
  PointerType *VoidPtrTy = PointerType::getUnqual(Int8Ty);

  Function *F_memset = M.getFunction("memset");
  if (!F_memset)
    return modified;

  // Scan through the module and replace memset() with pool_memset().
  for (Value::use_iterator UI = F_memset->use_begin(), UE = F_memset->use_end(); UI != UE;) {
    Instruction *I = dyn_cast<Instruction>(UI);
    ++UI; // Replacement invalidates the user, so must increment the iterator beforehand.

    if (I) {
      CallSite CS(I);
      Function *CalledF = CS.getCalledFunction();

      // Indirect call.
      if (NULL == CalledF)
        continue;

      if (F_memset != CalledF)
        continue;

      // Check that the function uses the correct number of arguments.
      if (CS.arg_size() != 3) {
        std::cerr << *I << std::endl;
        assert(CS.arg_size() == 3 && "memset() takes 3 arguments!\n");
        continue;
      }

      // Check for correct return type (void * == VoidPtrTy).
      if (CalledF->getReturnType() != VoidPtrTy)
        continue;

      // Get the exact type for size_t.
      const Type *IntTy = I->getOperand(2)->getType();
      const Type *SizeTy = I->getOperand(3)->getType();

      // Get pool handles for destination and source strings.
      Function *F = I->getParent()->getParent();
      PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
      Value *dstPH = dsnPass->getPoolHandle(I->getOperand(1), F, *FI);

      if (!dstPH)
        std::cerr << *I << std::endl;
      assert(dstPH && "No pool handle for destination pointer!\n");

      Value *Casted_dstPH = castTo(dstPH, VoidPtrTy, dstPH->getName() + ".casted", I);

      // Construct pool_memset().
      std::vector<const Type *> ParamTy(2, VoidPtrTy);
      ParamTy.push_back(IntTy);
      ParamTy.push_back(SizeTy);
      Value *Params[] = {Casted_dstPH, I->getOperand(1), I->getOperand(2), I->getOperand(3)};
      FunctionType *FT_pool_memset = FunctionType::get(F_memset->getReturnType(), ParamTy, false);
      Constant *F_pool_memset = M.getOrInsertFunction("pool_memset", FT_pool_memset);

      // Create the call instruction for pool_memset() and insert it before the current instruction.
      CallInst *CI_pool_memset = CallInst::Create(F_pool_memset, Params, array_endof(Params), "", I);
      
      // Replace all of the uses of memset() with pool_memset().
      I->replaceAllUsesWith(CI_pool_memset);
      I->eraseFromParent();

      // Record the transform.
      ++stat_transform_memset;

      // Mark the module as modified and continue to the next memset() call.
      modified = true;
    }
  }

  return modified;
}

/**
 * Secures strcpy() by transforming it into pool_strcpy() with correct bounds
 *
 * @param	M	Module from runOnModule() to scan for strcpy()
 * @return	Whether we modified the module
 */
bool StringTransform::strcpyTransform(Module &M) {
  // Flag whether we modified the module.
  bool modified = false;

  // Create needed pointer types.
  const Type *Int8Ty = IntegerType::getInt8Ty(getGlobalContext());
  PointerType *VoidPtrTy = PointerType::getUnqual(Int8Ty);

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

      // Get pool handles for destination and source strings.
      Function *F = I->getParent()->getParent();
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

/**
 * Secures strlen() by transforming it into pool_strlen() with correct bounds
 *
 * @param	M	Module from runOnModule() to scan for strlen()
 * @return	Whether we modified the module
 */
bool StringTransform::strlenTransform(Module &M) {
  // Flag whether we modified the module.
  bool modified = false;

  // Create needed pointer types.
  const Type *Int8Ty = IntegerType::getInt8Ty(getGlobalContext());
  const Type *Int32Ty = IntegerType::getInt32Ty(getGlobalContext());
  PointerType *VoidPtrTy = PointerType::getUnqual(Int8Ty);

  Function *F_strlen = M.getFunction("strlen");
  if (!F_strlen)
    return modified;

  // Scan through the module and replace strlen() with pool_strlen().
  for (Value::use_iterator UI = F_strlen->use_begin(), UE = F_strlen->use_end(); UI != UE;) {
    Instruction *I = dyn_cast<Instruction>(UI);
    ++UI; // Replacement invalidates the user, so must increment the iterator beforehand.

    if (I) {
      CallSite CS(I);
      Function *CalledF = CS.getCalledFunction();

      // Indirect call.
      if (NULL == CalledF)
        continue;

      if (F_strlen != CalledF)
        continue;

      // Check that the function uses the correct number of arguments.
      if (CS.arg_size() != 1) {
        std::cerr << *I << std::endl;
        assert(CS.arg_size() == 1 && "strlen() takes 1 argument!\n");
        continue;
      }

      // Check for correct return type (i32).
      if (CalledF->getReturnType() != Int32Ty)
        continue;

      Function *F = I->getParent()->getParent();
      PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
      Value *stringPH = dsnPass->getPoolHandle(I->getOperand(1), F, *FI);

      if (!stringPH)
        std::cerr << *I << std::endl;
      assert(stringPH && "No pool handle for string!\n");
      Value *Casted_stringPH = castTo(stringPH, VoidPtrTy, stringPH->getName() + ".casted", I);

      // Construct pool_strlen().
      std::vector<const Type *> ParamTy(2, VoidPtrTy); // SmallVector<const Type *, 4>
      Value *Params[] = {Casted_stringPH, I->getOperand(1)};
      FunctionType *FT_pool_strlen = FunctionType::get(F_strlen->getReturnType(), ParamTy, false);
      Constant *F_pool_strlen = M.getOrInsertFunction("pool_strlen", FT_pool_strlen);

      // Create the call instruction for pool_strlen() and insert it before the current instruction.
      CallInst *CI_pool_strlen = CallInst::Create(F_pool_strlen, Params, array_endof(Params), "", I);
      
      // Replace all of the uses of strlen() with pool_strlen().
      I->replaceAllUsesWith(CI_pool_strlen);
      I->eraseFromParent();

      // Record the transform.
      ++stat_transform_strlen;

      // Mark the module as modified and continue to the next strlen() call.
      modified = true;
    }
  }

  return modified;
}

/**
 * Secures strncpy() by transforming it into pool_strncpy() with correct bounds
 *
 * @param	M	Module from runOnModule() to scan for strncpy()
 * @return	Whether we modified the module
 */
bool StringTransform::strncpyTransform(Module &M) {
  // Flag whether we modified the module.
  bool modified = false;

  // Create needed pointer types.
  const Type *Int8Ty = IntegerType::getInt8Ty(getGlobalContext());
  PointerType *VoidPtrTy = PointerType::getUnqual(Int8Ty);

  Function *F_strncpy = M.getFunction("strncpy");
  if (!F_strncpy)
    return modified;

  // Scan through the module and replace strncpy() with pool_strncpy().
  for (Value::use_iterator UI = F_strncpy->use_begin(), UE = F_strncpy->use_end(); UI != UE;) {
    Instruction *I = dyn_cast<Instruction>(UI);
    ++UI; // Replacement invalidates the user, so must increment the iterator beforehand.

    if (I) {
      CallSite CS(I);
      Function *CalledF = CS.getCalledFunction();

      // Indirect call.
      if (NULL == CalledF)
        continue;

      if (F_strncpy != CalledF)
        continue;

      // Check that the function uses the correct number of arguments.
      if (CS.arg_size() != 3) {
        std::cerr << *I << std::endl;
        assert(CS.arg_size() == 3 && "strncpy() takes 3 arguments!\n");
        continue;
      }

      // Check for correct return type (char * == i8 * == VoidPtrTy).
      if (CalledF->getReturnType() != VoidPtrTy)
        continue;

      // Get the exact type for size_t.
      const Type *SizeTy = I->getOperand(3)->getType();

      // Get pool handles for destination and source strings.
      Function *F = I->getParent()->getParent();
      PA::FuncInfo *FI = paPass->getFuncInfoOrClone(*F);
      Value *dstPH = dsnPass->getPoolHandle(I->getOperand(1), F, *FI);
      Value *srcPH = dsnPass->getPoolHandle(I->getOperand(2), F, *FI);

      if (!dstPH || !srcPH)
        std::cerr << *I << std::endl;
      assert(dstPH && "No pool handle for destination pointer!\n");
      assert(srcPH && "No pool handle for source pointer!\n");

      Value *Casted_dstPH = castTo(dstPH, VoidPtrTy, dstPH->getName() + ".casted", I);
      Value *Casted_srcPH = castTo(srcPH, VoidPtrTy, srcPH->getName() + ".casted", I);

      // Construct pool_strncpy().
      std::vector<const Type *> ParamTy(4, VoidPtrTy);
      ParamTy.push_back(SizeTy);
      Value *Params[] = {Casted_dstPH, Casted_srcPH, I->getOperand(1), I->getOperand(2), I->getOperand(3)};
      FunctionType *FT_pool_strncpy = FunctionType::get(F_strncpy->getReturnType(), ParamTy, false);
      Constant *F_pool_strncpy = M.getOrInsertFunction("pool_strncpy", FT_pool_strncpy);

      // Create the call instruction for pool_strncpy() and insert it before the current instruction.
      CallInst *CI_pool_strncpy = CallInst::Create(F_pool_strncpy, Params, array_endof(Params), "", I);
      
      // Replace all of the uses of strncpy() with pool_strncpy().
      I->replaceAllUsesWith(CI_pool_strncpy);
      I->eraseFromParent();

      // Record the transform.
      ++stat_transform_strncpy;

      // Mark the module as modified and continue to the next strncpy() call.
      modified = true;
    }
  }

  return modified;
}


NAMESPACE_SC_END
