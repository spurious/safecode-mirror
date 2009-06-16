//===- DebugInstrumentation.h - Adds debug information to run-time checks ----//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass modifies calls to the pool allocator and SAFECode run-times to
// track source level debugging information.
//
//===----------------------------------------------------------------------===//

#ifndef DEBUG_INSTRUMENTATION_H
#define DEBUG_INSTRUMENTATION_H

#include "safecode/SAFECode.h"
#include "safecode/Intrinsic.h"
#include "llvm/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Type.h"
#include "llvm/Value.h"

#include <utility>

using namespace llvm;

NAMESPACE_SC_BEGIN

class GetSourceInfo {
  public:
    virtual std::pair<Value *, Value *> operator() (CallInst * I) = 0;
    virtual ~GetSourceInfo();
};

class LocationSourceInfo : public GetSourceInfo {
  public:
    virtual std::pair<Value *, Value *> operator() (CallInst * I);
};

class VariableSourceInfo : public GetSourceInfo {
  public:
    virtual std::pair<Value *, Value *> operator() (CallInst * I);
};

struct DebugInstrument : public ModulePass {
  public:
    static char ID;

    virtual bool runOnModule(Module &M);
    DebugInstrument () : ModulePass ((intptr_t) &ID) {
      return;
    }

    const char *getPassName() const {
      return "SAFECode Debug Instrumentation Pass";
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<InsertSCIntrinsic>();
      AU.setPreservesCFG();
      AU.setPreservesAll();
    };

  private:
    // LLVM type for void pointers (void *)
    Type * VoidPtrTy;

    // Private methods
    void transformFunction (Function * F, GetSourceInfo & SI);
};

NAMESPACE_SC_END

#endif
