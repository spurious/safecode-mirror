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

//
// To add a new function to the CStdLib checks, the following modifications are
// necessary:
// 
// In SAFECode:
//
//   - Add the pool_* prototype of the function to
//     runtime/include/CStdLibSupport.h.
//
//   - Implement the pool_* version of the function in the relevant file in
//     runtime/DebugRuntime.
//
//   - Add debug instrumentation information to
//     lib/DebugInstrumentation/DebugInstrumentation.cpp.
//
//   - Update the StringTransform pass to transform calls of the library
//     function into its pool_* version in lib/CStdLib/String.cpp.
//
// In poolalloc:
//
//   - Add an entry for the pool_* version of the function containing the
//     number of initial pool arguments to the structure in
//     include/dsa/CStdLib.h.
//
//   - Add an entry to lib/DSA/StdLibPass.cpp for the pool_* version of the
//     function to allow DSA to recognize it.
//


#include "safecode/CStdLib.h"
#include "safecode/Config/config.h"

using namespace llvm;

namespace llvm
{

// Identifier variable for the pass
char StringTransform::ID = 0;

// Statistics counters

STATISTIC(st_xform_memccpy, "Total memccpy() calls transformed");
STATISTIC(st_xform_memchr,  "Total memchr() calls transformed");
STATISTIC(st_xform_memcmp,  "Total memcmp() calls transformed");
STATISTIC(st_xform_memcpy,  "Total memcpy() calls transformed");
STATISTIC(st_xform_memmove, "Total memmove() calls transformed");
STATISTIC(st_xform_memset,  "Total memset() calls transformed");
STATISTIC(st_xform_strcat,  "Total strcat() calls transformed");
STATISTIC(st_xform_strchr,  "Total strchr() calls transformed");
STATISTIC(st_xform_strcmp,  "Total strcmp() calls transformed");
STATISTIC(st_xform_strcoll, "Total strcoll() calls transformed");
STATISTIC(st_xform_strcpy,  "Total strcpy() calls transformed");
STATISTIC(st_xform_strcspn, "Total strcspn() calls transformed");
// strerror_r
STATISTIC(st_xform_strlen,  "Total strlen() calls transformed");
STATISTIC(st_xform_strncat, "Total strncat() calls transformed");
STATISTIC(st_xform_strncmp, "Total strncmp() calls transformed");
STATISTIC(st_xform_strncpy, "Total strncpy() calls transformed");
STATISTIC(st_xform_strpbrk, "Total strpbrk() calls transformed");
STATISTIC(st_xform_strrchr, "Total strrchr() calls transformed");
STATISTIC(st_xform_strspn,  "Total strspn() calls transformed");
STATISTIC(st_xform_strstr,  "Total strstr() calls transformed");
STATISTIC(st_xform_strxfrm, "Total strxfrm() calls transformed");
// strtok, strtok_r, strxfrm

#ifdef HAVE_MEMPCPY
STATISTIC(st_xform_mempcpy,  "Total mempcpy() calls transformed");
#endif
#ifdef HAVE_STRCASESTR
STATISTIC(st_xform_strcasestr,  "Total strcasestr() calls transformed");
#endif
#ifdef HAVE_STPCPY
STATISTIC(st_xform_stpcpy,  "Total stpcpy() calls transformed");
#endif
#ifdef HAVE_STRNLEN
STATISTIC(st_xform_strnlen, "Total strnlen() calls transformed");
#endif

STATISTIC(st_xform_bcmp,    "Total bcmp() calls transformed");
STATISTIC(st_xform_bcopy,   "Total bcopy() calls transformed");
STATISTIC(st_xform_bzero,   "Total bzero() calls transformed");
STATISTIC(st_xform_index,   "Total index() calls transformed");
STATISTIC(st_xform_rindex,  "Total rindex() calls transformed");
STATISTIC(st_xform_strcasecmp,  "Total strcasecmp() calls transformed");
STATISTIC(st_xform_strncasecmp, "Total strncasecmp() calls transformed");

static RegisterPass<StringTransform>
ST("string_transform", "Secure C standard string library calls");

/**
 * Entry point for the LLVM pass that transforms C standard string library calls
 *
 * @param M Module to scan
 * @return  Whether we modified the module
 */
bool
StringTransform::runOnModule(Module &M)
{
  // Flags whether we modified the module.
  bool modified = false;

  tdata = &getAnalysis<TargetData>();

  // Create needed pointer types (char * == i8 * == VoidPtrTy).
  Type *VoidPtrTy = IntegerType::getInt8PtrTy(M.getContext());
  // Determine the type of size_t for functions that return this result.
  Type *SizeTTy = tdata->getIntPtrType(M.getContext());
  // Create other return types (int, void).
  Type *Int32Ty = IntegerType::getInt32Ty(M.getContext());
  Type *VoidTy  = Type::getVoidTy(M.getContext());

  // Functions from <string.h>
  modified |= transform(M, "memccpy", 4, 2, VoidPtrTy, st_xform_memccpy);
  modified |= transform(M, "memchr",  3, 1, VoidPtrTy, st_xform_memchr);
  modified |= transform(M, "memcmp",  3, 2, Int32Ty,   st_xform_memcmp);
  modified |= transform(M, "memcpy",  3, 2, Int32Ty,   st_xform_memcpy);
  modified |= transform(M, "memmove", 3, 2, VoidPtrTy, st_xform_memmove);
  modified |= transform(M, "memset",  2, 1, VoidPtrTy, st_xform_memset);
  modified |= transform(M, "strcat",  2, 2, VoidPtrTy, st_xform_strcat);
  modified |= transform(M, "strchr",  2, 1, VoidPtrTy, st_xform_strchr);
  modified |= transform(M, "strcmp",  2, 2, Int32Ty,   st_xform_strcmp);
  modified |= transform(M, "strcoll", 2, 2, Int32Ty,   st_xform_strcoll);
  modified |= transform(M, "strcpy",  2, 2, VoidPtrTy, st_xform_strcpy);
  modified |= transform(M, "strcspn", 2, 2, SizeTTy,   st_xform_strcspn);
  // modified |= handle_strerror_r(M);
  modified |= transform(M, "strlen",  1, 1, SizeTTy,   st_xform_strlen);
  modified |= transform(M, "strncat", 3, 2, VoidPtrTy, st_xform_strncat);
  modified |= transform(M, "strncmp", 3, 2, Int32Ty,   st_xform_strncmp);
  modified |= transform(M, "strncpy", 3, 2, VoidPtrTy, st_xform_strncpy);
  modified |= transform(M, "strpbrk", 2, 2, VoidPtrTy, st_xform_strpbrk);
  modified |= transform(M, "strrchr", 2, 1, VoidPtrTy, st_xform_strrchr);
  modified |= transform(M, "strspn",  2, 2, SizeTTy,   st_xform_strspn);
  modified |= transform(M, "strstr",  2, 2, VoidPtrTy, st_xform_strstr);
  modified |= transform(M, "strxfrm", 3, 2, SizeTTy,   st_xform_strxfrm);
  // Extensions to <string.h>
#ifdef HAVE_MEMPCPY
  modified |= transform(M, "mempcpy", 3, 2, VoidPtrTy, st_xform_mempcpy);
#endif
#ifdef HAVE_STRCASESTR
  modified |= transform(M, "strcasestr", 2, 2, VoidPtrTy, st_xform_strcasestr);
#endif
#ifdef HAVE_STPCPY
  modified |= transform(M, "stpcpy",  2, 2, VoidPtrTy, st_xform_stpcpy);
#endif
#ifdef HAVE_STRNLEN
  modified |= transform(M, "strnlen", 2, 1, SizeTTy,   st_xform_strnlen);
#endif
  // Functions from <strings.h>
  modified |= transform(M, "bcmp",    3, 2, Int32Ty,   st_xform_bcmp);
  modified |= transform(M, "bcopy",   3, 2, VoidTy,    st_xform_bcopy);
  modified |= transform(M, "bzero",   2, 1, VoidTy,    st_xform_bzero);
  modified |= transform(M, "index",   2, 1, VoidPtrTy, st_xform_index);
  modified |= transform(M, "rindex",  2, 1, VoidPtrTy, st_xform_rindex);
  modified |= transform(M, "strcasecmp", 2, 2, Int32Ty, st_xform_strcasecmp);
  modified |= transform(M, "strncasecmp", 3, 2, Int32Ty, st_xform_strncasecmp);

  return modified;
}

/**
 * Secures C standard string library calls by transforming them into
 * their corresponding runtime wrapper functions.
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
                           Statistic &statistic)
{
  // Check whether the number of pool arguments is small enough for all
  // pointer completeness information to be contained in one 8-bit quantity.
  assert(pool_argc <= 8 && "Unsupported number of pointer arguments!");

  Type *Int8Ty = IntegerType::getInt8Ty(M.getContext());
  // The pool handle type is a void pointer (i8 *).
  PointerType *VoidPtrTy = IntegerType::getInt8PtrTy(M.getContext());
  Function *F = M.getFunction(FunctionName);
  if (!F)
    return false; // Function does not exist in module.

  std::vector<Instruction *> toModify;
  std::vector<Instruction *>::iterator modifyIter, modifyEnd;

  // Scan through the module for calls of the desired function to transform.
  for (Value::use_iterator UI = F->use_begin(), UE = F->use_end();
       UI != UE;
       ++UI) {
    Instruction *I = dyn_cast<Instruction>(*UI);
    if (!I)
      continue;
    CallSite CS(I);
    Function *CalledF = CS.getCalledFunction();
    if (F != CalledF)
      continue;
    // Check that the function uses the correct number of arguments.
    assert(CS.arg_size() == argc && "Incorrect number of arguments!");
    // Check for correct return type.
    if (CalledF->getReturnType() != ReturnTy)
      continue;
    toModify.push_back(I);
  }

  // Return early if we've found nothing to modify.
  if (toModify.empty())
    return false;

  FunctionType *F_type = F->getFunctionType();
  // Build the type of the transformed function. This type has pool_argc
  // initial arguments of type i8 *.
  std::vector<Type *> ParamTy(pool_argc, VoidPtrTy);
  // Append the argument types of the original function to the new function's
  // argument types.
  for (unsigned i = 0; i < F_type->getNumParams(); ++i)
    ParamTy.push_back(F_type->getParamType(i));
  // Finally append the type for the completeness bit vector.
  ParamTy.push_back(Int8Ty);
  FunctionType *FT = FunctionType::get(F_type->getReturnType(), ParamTy, false);
  // Build the actual transformed function.
  Constant *F_pool = M.getOrInsertFunction("pool_" + FunctionName.str(), FT);

  // This is a placeholder value for the pool handles (to be "filled in" later
  // by poolalloc).
  Value *PH = ConstantPointerNull::get(VoidPtrTy);

  // Transform every valid use of the function that was found.
  for (modifyIter = toModify.begin(), modifyEnd = toModify.end();
       modifyIter != modifyEnd;
       ++modifyIter)
  {
    Instruction *I = cast<Instruction>(*modifyIter);
    // Construct vector of parameters to transformed function call.
    std::vector<Value *> Params;
    // Insert space for the pool handles.
    for (unsigned i = 0; i < pool_argc; i++)
      Params.push_back(PH);
    // Insert the original parameters.
    for (unsigned i = 0; i < argc; i++)
    {
      Value *f = I->getOperand(i);
      Params.push_back(f);
    }
    // Add the DSA completeness bitwise vector.
    Params.push_back(ConstantInt::get(Int8Ty, 0));
    // Create the call instruction for the transformed function and insert it
    // before the current instruction.
    CallInst *C = CallInst::Create(F_pool, Params, "", I);
    // Transfer debugging metadata if it exists from the old call into the new
    // one.
    if (MDNode *DebugNode = I->getMetadata("dbg"))
      C->setMetadata("dbg", DebugNode);
    // Replace all uses of the function with its transformed equivalent.
    I->replaceAllUsesWith(C);
    I->eraseFromParent();
    // Record the transformation.
    ++statistic;
  }

  // If we've reached here, the module has been modified.
  return true;
}

}
