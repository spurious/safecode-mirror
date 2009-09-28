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

#include "SCUtils.h" // castTo()

#include "llvm/Constants.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/STLExtras.h" // array_endof()
#include "llvm/Analysis/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/InstVisitor.h"

#include "safecode/PoolHandles.h"
#include "safecode/SAFECode.h"

#include <algorithm>
#include <iostream> // std::cerr
#include <vector>

using namespace llvm;

NAMESPACE_SC_BEGIN

/**
 * Pass that secures C standard library string calls via transforms
 */
class StringTransform : public ModulePass {
private:
  // Private methods
  bool memcpyTransform(Module &M);
  bool memmoveTransform(Module &M);
  bool mempcpyTransform(Module &M);
  bool memsetTransform(Module &M);
  bool strcpyTransform(Module &M);
  bool strlenTransform(Module &M);
  bool strncpyTransform(Module &M);

  // Private variables
  DSNodePass *dsnPass;
  PoolAllocateGroup *paPass;

public:
  static char ID;
  StringTransform() : ModulePass((intptr_t)&ID) {}
  virtual bool runOnModule(Module &M);

  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    // We require these passes to get information on pool handles
    AU.addRequired<DSNodePass>();
    DSNodePass::getAnalysisUsageForPoolAllocation(AU);

    // Pretend that we don't modify anything
    AU.setPreservesAll();
  }

  virtual void print(std::ostream &O, const Module *M) const {}
};

NAMESPACE_SC_END

#endif
