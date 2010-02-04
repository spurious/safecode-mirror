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

#ifndef OPTIMIZECHECKS_H
#define OPTIMIZECHECKS_H

#include "safecode/SAFECode.h"
#include "safecode/Intrinsic.h"
#include "safecode/PoolHandles.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/AliasAnalysis.h"
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

      // Pretend that we don't modify anything
      DSNodePass::preservePAandDSA(AU);
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
    // Pretend that we don't modify DSA and PA
		DSNodePass::preservePAandDSA(AU);
    AU.setPreservesCFG();
  }

 private:
    // References to required analysis passes
    InsertSCIntrinsic * intrinsic;
    Function *ExactCheck2;
    bool visitCheckingIntrinsic(CallInst * CI);
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
  virtual bool runOnModule (Module & M);
  const char *getPassName() const {
    return "Pool Register Elimination";
  }
  
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    // We need to know about SAFECode intrinsics
    AU.addRequired<InsertSCIntrinsic>();
    AU.addRequired<AliasAnalysis>();
    // Pretend that we don't modify anything
    AU.setPreservesCFG();
  }

 private:
  // References to required analysis passes
  InsertSCIntrinsic * intrinsic;
  AliasAnalysis * AA;
  AliasSetTracker * AST;

  //
  // Data structure: usedSet
  //
  // Description:
  //  This set contains all AliasSets which are used in run-time checks that
  //  perform an object lookup.  It conservatively tell us which pointers must
  //  be registered with the SAFECode run-time.
  //
  DenseSet<AliasSet*> usedSet;

  // Private methods
  void markUsedAliasSet(const char * name);
  void removeUnusedRegistrations (void);
  bool isSafeToRemove (Value * Ptr);
  void findCheckedAliasSets ();
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
