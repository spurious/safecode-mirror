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

#if 0
STATISTIC(stat_transform_memcpy, "Total memcpy() calls transformed");
STATISTIC(stat_transform_memmove, "Total memmove() calls transformed");
STATISTIC(stat_transform_mempcpy, "Total mempcpy() calls transformed");
STATISTIC(stat_transform_memset, "Total memset() calls transformed");
STATISTIC(stat_transform_strlcat, "Total strlcat() calls transformed");
STATISTIC(stat_transform_strlcpy, "Total strlcpy() calls transformed");
STATISTIC(stat_transform_wcscpy, "Total wcscpy() calls transformed");
STATISTIC(stat_transform_wmemcpy, "Total wmemcpy() calls transformed");
STATISTIC(stat_transform_wmemmove, "Total wmemmove() calls transformed");
#endif

STATISTIC(stat_transform_strncpy, "Total strncpy() calls transformed");
STATISTIC(stat_transform_strcpy, "Total strcpy() calls transformed");
STATISTIC(stat_transform_strlen, "Total strlen() calls transformed");
STATISTIC(stat_transform_strnlen, "Total strnlen() calls transformed");
STATISTIC(stat_transform_strchr,  "Total strchr() calls transformed");
STATISTIC(stat_transform_strrchr, "Total strrchr() calls transformed");
STATISTIC(stat_transform_strcat,  "Total strcat() calls transformed");
STATISTIC(stat_transform_strncat, "Total strncat() calls transformed");
STATISTIC(stat_transform_strstr,  "Total strstr() calls transformed");
STATISTIC(stat_transform_strpbrk, "Total strpbrk() calls transformed");
STATISTIC(stat_transform_strcmp,  "Total strcmp() calls transformed");
STATISTIC(stat_transform_strncmp, "Total strncmp() calls transformed");
STATISTIC(stat_transform_memcmp,  "Total memcmp() calls transformed");
STATISTIC(stat_transform_strcasecmp,  "Total strcasecmp() calls transformed");
STATISTIC(stat_transform_strncasecmp, "Total strncasecmp() calls transformed");
STATISTIC(stat_transform_strspn,  "Total strspn() calls transformed");
STATISTIC(stat_transform_strcspn,  "Total strcspn() calls transformed");
STATISTIC(stat_transform_memccpy, "Total memccpy() calls transformed");
STATISTIC(stat_transform_memchr, "Total memchr() calls transformed");


static RegisterPass<StringTransform>
ST("string_transform", "Secure C standard string library calls");


/**
 * Entry point for the LLVM pass that transforms C standard string library calls
 *
 * @param M Module to scan
 * @return  Whether we modified the module
 */
bool
StringTransform::runOnModule(Module &M) {
  // Flags whether we modified the module.
  bool modified = false;

  tdata = &getAnalysis<TargetData>();

  // Create needed pointer types (char * == i8 * == VoidPtrTy).
  const Type *Int8Ty  = IntegerType::getInt8Ty(M.getContext());
  PointerType *VoidPtrTy = PointerType::getUnqual(Int8Ty);
  // Determine the size of size_t for functions that return this result.
  const Type *SizeTTy = tdata->getIntPtrType(M.getContext());
  // Determine the size of int for functions like strcmp, strncmp and etc..
  const Type *Int32ty = IntegerType::getInt32Ty(M.getContext());

  modified |= transform(M, "strcpy",  2, 2, VoidPtrTy, stat_transform_strcpy);
  modified |= transform(M, "strncpy", 3, 2, VoidPtrTy, stat_transform_strncpy);
  modified |= transform(M, "strlen",  1, 1, SizeTTy, stat_transform_strlen);
  modified |= transform(M, "strnlen", 2, 1, SizeTTy, stat_transform_strnlen);
  modified |= transform(M, "strchr",  2, 1, VoidPtrTy, stat_transform_strchr);
  modified |= transform(M, "strrchr", 2, 1, VoidPtrTy, stat_transform_strrchr);
  modified |= transform(M, "strcat",  2, 2, VoidPtrTy, stat_transform_strcat);
  modified |= transform(M, "strncat", 3, 2, VoidPtrTy, stat_transform_strncat);
  modified |= transform(M, "strstr",  2, 2, VoidPtrTy, stat_transform_strstr);
  modified |= transform(M, "strpbrk", 2, 2, VoidPtrTy, stat_transform_strpbrk);
  modified |= transform(M, "strcmp",  2, 2, Int32ty, stat_transform_strcmp);
  modified |= transform(M, "strncmp", 3, 2, Int32ty, stat_transform_strncmp);
  modified |= transform(M, "memcmp",  3, 2, Int32ty, stat_transform_memcmp);
  modified |= transform(M, "strcasecmp",  2, 2, Int32ty,
    stat_transform_strcasecmp);
  modified |= transform(M, "strncasecmp", 3, 2, Int32ty,
    stat_transform_strncasecmp);
  modified |= transform(M, "strcspn",  2, 2, SizeTTy, stat_transform_strcspn);
  modified |= transform(M, "strspn",  2, 2, SizeTTy, stat_transform_strspn);
  modified |= transform(M, "memccpy", 4, 2, VoidPtrTy, stat_transform_memccpy);
  modified |= transform(M, "memchr", 3, 1, VoidPtrTy, stat_transform_memchr);

#if 0
  modified |= transform(M, "memcpy", 3, 2, VoidPtrTy, stat_transform_memcpy);
  modified |= transform(M, "memmove", 3, 2, VoidPtrTy, stat_transform_memmove);
  modified |= transform(M, "mempcpy", 3, 2, VoidPtrTy, stat_transform_mempcpy);
  modified |= transform(M, "memset", 3, 1, VoidPtrTy, stat_transform_memset);
#endif


  return modified;
}

/**
 * Secures C standard string library calls by transforming them into
 * their corresponding runtime checks.
 *
 * In particular, after a call of
 *
 *   transform(M, "f", X, N, ReturnType, stat)
 *
 * where X is the number of arguments of f, all calls to f with the prototype
 *
 *  ReturnType f(char *str1, ..., char *strN, [non-string arguments]);
 *
 * will be transformed into calls of to the function pool_f with the prototype
 *
 *  ReturnType pool_f(void *pool1, ..., void *poolN,
 *                    char *str1, ..., char *strN,
 *                    [non-string arguments], uint8_t complete);
 *
 *
 * @param M              Module from runOnModule() to scan for functions to
 *                       transform
 * @param FunctionName   The name of the library function to transform.
 * @param argc           The total number of arguments to the function.
 * @param pool_argc      The number of initial pointer arguments for which
 *                       to insert pools in the transformed call (currently
 *                       required to be at most 8).
 * @param ReturnTy       The return type of the calls to transform.
 * @param statistic      A reference to the relevant transform statistic.
 * @return               Returns true if any calls were transformed, and
 *                       false if no changes were made.
 */
bool
StringTransform::transform(Module &M,
                           const StringRef FunctionName,
                           const unsigned argc,
                           const unsigned pool_argc,
                           const Type *ReturnTy,
                           Statistic &statistic) {
  // Flag whether we modified the module.
  bool modified = false;

  // Create void pointer type for null pool handle.
  const Type *Int8Ty = IntegerType::getInt8Ty(M.getContext());
  PointerType *VoidPtrTy = PointerType::getUnqual(Int8Ty);

  Function *F = M.getFunction(FunctionName);
  if (!F)
    return modified;

  // Scan through the module for the desired function to transform.
  for (Value::use_iterator UI = F->use_begin(), UE = F->use_end(); UI != UE;) {
    Instruction *I = dyn_cast<Instruction>(UI);
    ++UI; // Replacement invalidates the user,
          // so must increment the iterator beforehand.

    if (!I)
      continue;

    unsigned char complete = 0;
    CallSite CS(I);
    Function *CalledF = CS.getCalledFunction();

    if (F != CalledF)
      continue;

    // Check that the function uses the correct number of arguments.
    if (CS.arg_size() != argc) {
      I->dump();
      assert(CS.arg_size() == argc && "Incorrect number of arguments!");
      continue;
    }

    // Check for correct return type.
    if (CalledF->getReturnType() != ReturnTy)
      continue;

    // Create null pool handles.
    Value *PH = ConstantPointerNull::get(VoidPtrTy);

    // Construct vector of parameters to transformed function call.
    SmallVector<Value *, 6> Params;

    if (pool_argc <= 8) {
      // Insert space for the pool handles.
      for (unsigned i = 1; i <= pool_argc; i++) {
        Params.push_back(PH);
      }
      // Insert the original parameters.
      for (unsigned i = 1; i <= pool_argc; i++) {
        Params.push_back(I->getOperand(i));
      }
    } else {
      assert("Unsupported number of pool arguments!");
      continue;
    }

    // FunctionType::get() needs std::vector<>
    std::vector<const Type *> ParamTy(2 * pool_argc, VoidPtrTy);

    const Type *Ty = NULL;

    // Add the remaining (non-pool) arguments.
    if (argc > pool_argc) {
      unsigned i = argc - pool_argc;

      for (; i > 0; --i) {
        Params.push_back(I->getOperand(argc - i + 1));
        Ty = I->getOperand(argc - i + 1)->getType();
        ParamTy.push_back(Ty);
      }
    }

    // Add the DSA completeness bitwise vector.
    Params.push_back(ConstantInt::get(Int8Ty, complete));
    ParamTy.push_back(Int8Ty);

    // Construct the transformed function.
    FunctionType *FT = FunctionType::get(F->getReturnType(), ParamTy, false);
    Constant *F_pool = M.getOrInsertFunction("pool_" + FunctionName.str(), FT);

    // Create the call instruction for the transformed function and insert it
    // before the current instruction.
    CallInst *C = CallInst::Create(F_pool, Params.begin(), Params.end(), "", I);
    
    // Replace all uses of the function with its transformed equivalent.
    I->replaceAllUsesWith(C);
    I->eraseFromParent();

    // Record the transform.
    ++statistic;

    // Mark the module as modified and continue to the next function call.
    modified = true;
  }

  return modified;
}

NAMESPACE_SC_END
