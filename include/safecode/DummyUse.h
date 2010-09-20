//===- DummyUse.h - Dummy Pass for SAFECode ----------------------------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a dummy pass.  It does nothing except keep the pool
// allocation "analysis" results alive for subsequent passes.
//
//===----------------------------------------------------------------------===//

#ifndef _SAFECODE_DUMMYUSE_H
#define _SAFECODE_DUMMYUSE_H

#include "safecode/SAFECode.h"
#include "safecode/SAFECodeConfig.h"
#include "safecode/InsertChecks.h"

#include "poolalloc/PoolAllocate.h"

#include "llvm/Pass.h"

NAMESPACE_SC_BEGIN

class DummyUse : public ModulePass {
public:
  static char ID;
  DummyUse() : ModulePass((intptr_t)&ID) {}
  virtual void getAnalysisUsage(AnalysisUsage & AU) const {
    DSNodePass::getAnalysisUsageForDSA(AU);
    AU.addRequired<PoolAllocateGroup>();
    AU.setPreservesAll();
  }
  virtual bool runOnModule(Module &) { return false; }
};

NAMESPACE_SC_END

#endif
