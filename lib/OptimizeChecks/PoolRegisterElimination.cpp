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

static RegisterPass<PoolRegisterElimination> X ("poolreg-elim", "Pool Register Eliminiation");

// Pass Statistics
namespace {
  STATISTIC (RemovedRegistration ,    "Removed object registration and deregistraion");
}

// The set to track whether there are checking intrinsics used the splay tree
// for some pointer in the alias set.
static DenseSet<AliasSet*> usedSet;

bool
PoolRegisterElimination::runOnModule(Module & M) {
  usedSet.clear();
  RemovedRegistration = 0;
  intrinsic = &getAnalysis<InsertSCIntrinsic>();
  AA = &getAnalysis<AliasAnalysis>();
  AST = new AliasSetTracker(*AA);

  // TODO: This should be tagged at the intrinsic pass

  const char * splayTreeCheckIntrinsics[] = 
    {"sc.lscheck", "sc.lscheckui", "sc.lscheckalign", "sc.lscheckalignui",
     "sc.boundscheck", "sc.boundscheckui" };

  for (size_t i = 0; i < sizeof(splayTreeCheckIntrinsics) / sizeof (const char*); ++i) {
    markUsedAliasSet(splayTreeCheckIntrinsics[i]);
  }

  const char * registerIntrinsics[] = 
    {"sc.pool_register", "sc.pool_unregister"};

  for (size_t i = 0; i < sizeof(registerIntrinsics) / sizeof (const char*); ++i) {
    removeUnusedRegistration(registerIntrinsics[i]);
  }

  delete AST;
  return true;
}

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
