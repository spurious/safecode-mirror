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
 * Secures strcpy() by transforming it into strncpy with correct bounds
 *
 * @param	M	Module from runOnModule() to scan for strcpy()
 * @return	Whether we modified the module
 */
bool StringTransform::strcpyTransform(Module &M) {
  // Flags whether we modified the module
  bool modified = false;

  std::vector<const Type *> ParamTy;
  ParamTy.push_back(PointerType::getUnqual(Type::Int8Ty));
  ParamTy.push_back(PointerType::getUnqual(Type::Int8Ty));
  ParamTy.push_back(PointerType::getUnqual(Type::Int8Ty));
  ParamTy.push_back(PointerType::getUnqual(Type::Int8Ty));

  Function * strcpyFunc = M.getFunction("strcpy");
  if (!strcpyFunc)
    return modified;
  const Type * strncpyRT = strcpyFunc->getReturnType();
  FunctionType * strncpyFT = FunctionType::get(strncpyRT, ParamTy, false);
  Constant * strncpyFunction = M.getOrInsertFunction("strncpy", strncpyFT);

  // Scan through the module and replace strcpy() with strncpy().
  for (Module::iterator F=M.begin(); F != M.end(); ++F) {
    for (Function::iterator BB = F->begin(); BB != F->end(); ++BB) {
      for (BasicBlock::iterator I = BB->begin(); I != BB->end(); ++I) {
        // If this is not a call or invoke instruction, just skip it.
        if (!(isa<CallInst>(I) || isa<InvokeInst>(I)))
          continue;

        // Otherwise, figure out if this is a direct call to strcpy().
        CallInst * CI = dyn_cast<CallInst>(I);
        Function * CalledFunc = CI->getCalledFunction();

        if (CalledFunc == 0 || !CalledFunc->hasName())
          continue;

        if (CalledFunc->getName() != "strcpy")
          continue;

        // Create a strncpy() call.
        std::vector<Value *> Params;
        Params.push_back(CI->getOperand(1));
        Params.push_back(CI->getOperand(2));
        Params.push_back(CI->getOperand(1));
        Params.push_back(CI->getOperand(2));
        CallInst * strncpyCallInst = CallInst::Create(strncpyFunction, Params.begin(), Params.end(), "strings", CI);

        // Replace all of the uses of the strcpy() call with the new strncpy() call.
        CI->replaceAllUsesWith(strncpyCallInst);
        CI->eraseFromParent();

        // Record the transform.
        ++stat_transform_strcpy;

        // Mark the module as modified and continue to the next strcpy() call.
        modified = true;
      }
    }
  }

  return modified;
}

NAMESPACE_SC_END
