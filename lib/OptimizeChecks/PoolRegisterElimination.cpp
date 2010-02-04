//===- PoolRegisterElimination.cpp ---------------------------------------- --//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
//  This pass eliminates unnessary poolregister() / poolunregister() in the
//  code. Redundant poolregister() happens when there are no boundscheck() /
//  poolcheck() on a certain GEP, possibly all of these checks are lowered to
//  exact checks.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "poolreg-elim"
#include "safecode/OptimizeChecks.h"
#include "safecode/Support/AllocatorInfo.h"
#include "SCUtils.h"

#include "llvm/Analysis/AliasSetTracker.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/Statistic.h"


NAMESPACE_SC_BEGIN

static RegisterPass<PoolRegisterElimination>
X ("poolreg-elim", "Pool Register Eliminiation");

// Pass Statistics
namespace {
  STATISTIC (RemovedRegistration,
  "Number of object registrations/deregistrations removed");
}

//
// Data structure: usedSet
//
// Description:
//  This set contains all AliasSets which are used in run-time checks that
//  perform an object lookup.  It conservatively tell us which pointers must
//  be registered with the SAFECode run-time.
//
static DenseSet<AliasSet*> usedSet;

bool
PoolRegisterElimination::runOnModule(Module & M) {
  usedSet.clear();
  intrinsic = &getAnalysis<InsertSCIntrinsic>();
  AA = &getAnalysis<AliasAnalysis>();
  AST = new AliasSetTracker(*AA);

  // FIXME: The list of intrinsics should be selected via scanning through the
  // intrinsic lists with specified flags.

  const char * splayTreeCheckIntrinsics[] = {
    "sc.lscheck",
    "sc.lscheckui",
    "sc.lscheckalign",
    "sc.lscheckalignui",
    "sc.boundscheck",
    "sc.boundscheckui"
  };

  //
  // Find all of the pointers that are used by run-time checks which require an
  // object lookup.  Mark their alias sets as being checked; this ensures that
  // any pointers aliasing with checked pointers are registered.
  //
  for (size_t i = 0;
       i < sizeof(splayTreeCheckIntrinsics) / sizeof (const char*);
       ++i) {
    markUsedAliasSet(splayTreeCheckIntrinsics[i]);
  }

  //
  // List of registration intrinsics.
  //
  // FIXME:
  //  It is possible that removeUnusedRegistration() will properly detect
  //  that pointers *within* argv are not used.  This should be investigated
  //  before sc.pool_argvregister() is added back into the list.
  //
  // Note that sc.pool_argvregister() is not in this list.  This is because
  // it registers both the argv array and all the command line arguments whose
  // pointers are within the argv array.
  //
  const char * registerIntrinsics[] = {
    "sc.pool_register",
    "sc.pool_register_stack",
    "sc.pool_register_global",
    "sc.pool_unregister",
    "sc.pool_unregister_stack",
  };

  //
  // Process each registration function by removing those objects that are
  // are not checked by any run-time function requiring an object lookup.
  //
  for (size_t i = 0;
       i < sizeof(registerIntrinsics) / sizeof (const char*);
       ++i) {
    removeUnusedRegistration(registerIntrinsics[i]);
  }

  delete AST;
  return true;
}

//
// Method: markUsedAliasSet
//
// Description:
//  This method takes the name of a run-time check and determines which alias
//  sets are ever passed into the function.
//
// Inputs:
//  name - The name of the run-time function for which to find uses.
//
// Side-effects:
//  Any alias sets that are checked by the specified run-time function will
//  have been added to the usedSet variable.
//
void
PoolRegisterElimination::markUsedAliasSet(const char * name) {
  Function * F = intrinsic->getIntrinsic(name).F;

  for(Value::use_iterator UI=F->use_begin(), UE=F->use_end(); UI != UE; ++UI) {
    CallInst * CI = cast<CallInst>(*UI);
    Value * checkedPtr = intrinsic->getValuePointer(CI);
    AliasSet & aliasSet = AST->getAliasSetForPointer(checkedPtr, 0);
    usedSet.insert(&aliasSet);
  }
}

//
// Method: removeUnusedRegistration()
//
// Description:
//  This method take the name of a registration function and removes all
//  registrations made with that function for pointers that are never checked.
//
// Inputs:
//  name - The name of the registration intrinsic.
//
void
PoolRegisterElimination::removeUnusedRegistration(const char * name) {
  Function * F = intrinsic->getIntrinsic(name).F;
  std::vector<CallInst*> toBeRemoved;
  for(Value::use_iterator UI=F->use_begin(), UE=F->use_end(); UI != UE; ++UI) {
    CallInst * CI = cast<CallInst>(*UI);
    Value * checkedPtr = intrinsic->getValuePointer(CI);
    AliasSet * aliasSet = AST->getAliasSetForPointerIfExists(checkedPtr, 0);
    if (usedSet.count(aliasSet) == 0) {
      toBeRemoved.push_back(CI);
    }
  }

  RemovedRegistration += toBeRemoved.size();
  for(std::vector<CallInst*>::iterator it = toBeRemoved.begin(), end = toBeRemoved.end(); it != end; ++it) {
    (*it)->eraseFromParent();
  }
}

char PoolRegisterElimination::ID = 0;

NAMESPACE_SC_END
