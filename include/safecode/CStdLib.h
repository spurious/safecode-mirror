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

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/Passes.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InstVisitor.h"
#include "llvm/Target/TargetData.h"

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

  // Private variables
  TargetData *tdata;

public:
  static char ID;
  StringTransform() : ModulePass((intptr_t)&ID) {}
  virtual bool runOnModule(Module &M);

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    // Require TargetData
    AU.addRequired<TargetData>();

    // No modification of control flow graph
    AU.setPreservesCFG();
  }
};

NAMESPACE_SC_END

#endif
