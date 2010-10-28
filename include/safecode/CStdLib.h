//===------------ CStdLib.h - Secure C standard library calls -------------===//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass finds all calls to functions in the C standard library and
// transforms them to a more secure form.
//
//===----------------------------------------------------------------------===//

#ifndef CSTDLIB_H
#define CSTDLIB_H

#include "dsa/DSSupport.h"

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InstVisitor.h"

#include "safecode/PoolHandles.h"
#include "safecode/SAFECode.h"

#include <vector>

using namespace llvm;

NAMESPACE_SC_BEGIN

/**
 * Pass that secures C standard library string calls via transforms
 */
class StringTransform : public ModulePass {
private:
  // Private methods
  bool transform(Module &M, const StringRef FunctionName, const unsigned argc, const unsigned pool_argc, const Type *ReturnTy, Statistic &statistic);

  // DSA
  unsigned getDSFlags(const Value *V, const Function *F);

  // Private variables
  EQTDDataStructures *dsaPass;

public:
  static char ID;
  StringTransform() : ModulePass((intptr_t)&ID) {}
  virtual bool runOnModule(Module &M);

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    // Require DSA
    AU.addRequired<EQTDDataStructures>();

    // No modification of control flow graph
    AU.setPreservesCFG();
  }
};

NAMESPACE_SC_END

#endif
