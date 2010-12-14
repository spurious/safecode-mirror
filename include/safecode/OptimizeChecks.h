//===- OptimizeChecks.cpp - Optimize SAFECode Run-time Checks -----*- C++ -*--//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This pass optimizes some of the SAFECode run-time checks.
//
//===----------------------------------------------------------------------===//

#ifndef SAFECODE_OPTIMIZECHECKS_H
#define SAFECODE_OPTIMIZECHECKS_H

#include "safecode/SAFECode.h"
#include "safecode/Intrinsic.h"
#include "safecode/PoolHandles.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Module.h"
#include "llvm/Pass.h"

using namespace llvm;

namespace llvm {
  class AliasSetTracker;
}

NAMESPACE_SC_BEGIN

//
// FIXME: Obviously a renamed is required!
// Pass: OptimizeChecks
//
// Description:
//  This pass examines the run-time checks that SAFECode has inserted into a
//  program and attempts to remove checks that are unnecessary.
//
struct OptimizeChecks : public ModulePass {
  private:
    // Private methods
    bool processFunction (Function * F);
    bool onlyUsedInCompares (Value * Val);

    // References to required analysis passes
    InsertSCIntrinsic * intrinPass;

    // The set of GEP checking functions
    std::vector<Function *> GEPCheckingFunctions;

  public:
    static char ID;
    OptimizeChecks() : ModulePass((intptr_t)(&ID)) {}
    virtual bool runOnModule (Module & M);

    const char *getPassName() const {
      return "Optimize SAFECode Run-time Checks";
    }

    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      // We need to know about SAFECode intrinsics
      AU.addRequired<InsertSCIntrinsic>();
      AU.setPreservesCFG();
    }
};

//
// Pass: ExactCheckOpt
//
// Description:
//  This pass tries to lower bounds checks and load/store checks to exact
//  checks, that is checks whose bounds information can be determined easily,
//  say, allocations inside a function or global variables. Therefore SAFECode
//  does not need to register stuffs in the meta-data.
//
struct ExactCheckOpt : public ModulePass {
 public:
  static char ID;
 ExactCheckOpt() : ModulePass((intptr_t)(&ID)) {}
  virtual bool runOnModule (Module & M);
  const char *getPassName() const {
    return "Exact check optimization";
  }
  
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    // We need to know about SAFECode intrinsics
    AU.addRequired<InsertSCIntrinsic>();
    AU.setPreservesCFG();
  }

 private:
    // References to required analysis passes
    InsertSCIntrinsic * intrinsic;
    Function *ExactCheck2;
    bool visitCheckingIntrinsic(CallInst * CI, bool isMemCheck);
    void rewriteToExactCheck(CallInst * CI, Value * BasePointer, 
                             Value * ResultPointer, Value * Bounds);
    std::vector<CallInst*> checkingIntrinsicsToBeRemoved;
};

//
// Pass: PoolRegisterElimination
//
// Description:
//  This pass eliminate unnessary poolregister() / poolunregister() in the
//  code. Redundant poolregister() happens when there are no boundscheck() /
//  poolcheck() on a certain GEP, possibly all of these checks are lowered to
//  exact checks.
//
struct PoolRegisterElimination : public ModulePass {
 public:
  static char ID;
  PoolRegisterElimination() : ModulePass((intptr_t)(&ID)) {}
  PoolRegisterElimination(char * ID) : ModulePass((intptr_t)(ID)) {}
  virtual bool runOnModule (Module & M);
  const char *getPassName() const {
    return "Pool Register Elimination";
  }
  
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    // We need to know about SAFECode intrinsics
    AU.addRequired<InsertSCIntrinsic>();

    // We need DSA to tell us about memory object
    AU.addRequired<EQTDDataStructures>();

    // We don't modify the control-flow graph
    AU.setPreservesCFG();
  }

 protected:
  // References to required analysis passes
  InsertSCIntrinsic * intrinsic;
  EQTDDataStructures * dsaPass;

  // Set of globals which do not need to be registered
  std::set<GlobalVariable *> SafeGlobals;

  // Protected methods
  template<typename insert_iterator>
  void findSafeGlobals (Module & M, insert_iterator InsertPt);

  void removeTypeSafeRegistrations (const char * name);
  void removeSingletonRegistrations (const char * name);
  void removeUnusedRegistrations (const char * name);
  bool isSafeToRemove (Value * Ptr);
};

//
// Pass: DebugPoolRegisterElimination
//
// Description:
//  This pass is identical to the PoolRegisterElimination pass except that it
//  will not disrupt the debugging features of the SAFECode debug tool.  It
//  aims to provide some optimization while providing good debug information.
//
struct DebugPoolRegisterElimination : public PoolRegisterElimination {
 public:
  static char ID;
  DebugPoolRegisterElimination() : PoolRegisterElimination(&ID) {}
  virtual bool runOnModule (Module & M);
  const char *getPassName() const {
    return "Debugging-Safe Pool Register Elimination";
  }

 protected:
  // Protected methods
  void findFreedAliasSets (void);
};

//
// Pass: Unused Check Elimination
//
// Description:
//  Kill all the checks with zero uses.
//
struct UnusedCheckElimination : public ModulePass {
 public:
  static char ID;
  UnusedCheckElimination() : ModulePass((intptr_t)(&ID)) {}
  virtual bool runOnModule (Module & M);
  const char *getPassName() const {
    return "Unused Check Elimination";
  }
  
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    // We need to know about SAFECode intrinsics
    AU.addRequired<InsertSCIntrinsic>();
    AU.setPreservesCFG();
  }

 private:
  // References to required analysis passes
  InsertSCIntrinsic * intrinsic;
  std::vector<CallInst *> unusedChecks;
};

NAMESPACE_SC_END

#endif
