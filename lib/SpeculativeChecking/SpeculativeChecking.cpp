/// This file define a pass to lower synchronous checking calls to
/// speculative checking calls

#include <iostream>
#include <set>
#include <map>
#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"
#include "safecode/Config/config.h"
#include "safecode/SpeculativeChecking.h"
#include "safecode/VectorListHelper.h"

using namespace llvm;

char SpeculativeCheckingInsertSyncPoints::ID = 0;

/// Static Members
namespace {
  typedef std::set<std::string> CheckFuncSetTy;
  typedef std::set<std::string> SafeFuncSetTy;
  SafeFuncSetTy sSafeFuncSet;
  CheckFuncSetTy sCheckFuncSet;
  Constant * sFuncWaitForSyncToken;
}


// here are the functions are considered as "safe",
// either we know the semantics of them or they are not handled
// TODO: add stuffs like strlen / strcpy / strncpy
static const char * safeFunctions[] = {
//  "__sc_par_poolinit", "pool_init_runtime",
  "exactcheck", "exactcheck2",
  "memset",
  "llvm.memcpy.i32", "llvm.memcpy.i64",
  "llvm.memset.i32", "llvm.memset.i64",
  "llvm.memmove.i32", "llvm.memmove.i64",
  "memcmp"
};

// Functions used in checking
static const char * checkingFunctions[] = {
  "__sc_par_poolregister", "__sc_par_poolunregister",
  "__sc_par_poolcheck", "__sc_par_poolcheckui",
  "__sc_par_boundscheck", "__sc_par_boundscheckui",
  "__sc_par_poolalloc", "__sc_par_poolrealloc",
  "__sc_par_poolstrdup", "__sc_par_poolcalloc",
  "__sc_par_poolfree"
};

// A simple HACK to remove redudant synchronization points in this cases:
//
// call external @foo
// spam... but does not do any pointer stuffs
// call external @bar
// 
// we only need to insert a sync point before foo

static bool sHaveSeenCheckingCall;

namespace llvm {
cl::opt<bool> OptimisticSyncPoint ("optimistic-sync-point",
                                      cl::init(false),
                                      cl::desc("Place synchronization points only before external functions"));

  ////////////////////////////////////////////////////////////////////////////
  // SpeculativeCheckingInsertSyncPoints Methods
  ////////////////////////////////////////////////////////////////////////////

  bool
  SpeculativeCheckingInsertSyncPoints::doInitialization(Module & M) {
    sFuncWaitForSyncToken =
      M.getOrInsertFunction("__sc_par_wait_for_completion", 
            FunctionType::get(Type::VoidTy,
                  std::vector<const Type*>(), false)
                  );
    
    for (size_t i = 0; i < sizeof(checkingFunctions) / sizeof(const char *); ++i) {
      sCheckFuncSet.insert(checkingFunctions[i]);
    }

    for (size_t i = 0; i < sizeof(safeFunctions) / sizeof(const char *); ++i) {
      sSafeFuncSet.insert(safeFunctions[i]);
    }
    return true;
  }

  bool
  SpeculativeCheckingInsertSyncPoints::runOnBasicBlock(BasicBlock & BB) {
    bool changed = false;
    sHaveSeenCheckingCall = true;
    typedef bool (SpeculativeCheckingInsertSyncPoints::*HandlerTy)(CallInst*);
    static HandlerTy handler = OptimisticSyncPoint ? 
      &SpeculativeCheckingInsertSyncPoints::insertSyncPointsBeforeExternalCall
      : &SpeculativeCheckingInsertSyncPoints::insertSyncPointsAfterCheckingCall;

    for (BasicBlock::iterator I = BB.begin(); I != BB.end(); ++I) {
      if (CallInst * CI = dyn_cast<CallInst>(I)) {
        changed  |= (this->*handler)(CI);
      }
    }
    return changed;
  }

  // FIXME: Handle the indirect call cases.

  bool
  SpeculativeCheckingInsertSyncPoints::insertSyncPointsBeforeExternalCall(CallInst * CI) {
    Function *F = CI->getCalledFunction();
    Value * Fop = CI->getOperand(0);
    const std::string & FName = Fop->getName();
    bool checkingCall = isCheckingCall(FName);
    sHaveSeenCheckingCall |= checkingCall; 

    // in the exception list?
    SafeFuncSetTy::const_iterator it = sSafeFuncSet.find(FName);
    if (it != sSafeFuncSet.end() || checkingCall) return false;

    if (F && !(F->isDeclaration())) return false;
    if (F && isSafeFunction(F)) return false;

    // TODO: Skip some intrinsic, like pow / exp

    if (sHaveSeenCheckingCall) {
      CallInst::Create(sFuncWaitForSyncToken, "", CI);
      sHaveSeenCheckingCall = false;
    }
    return true;
  }

  bool
  SpeculativeCheckingInsertSyncPoints::insertSyncPointsAfterCheckingCall(CallInst * CI) {
    Function *F = CI->getCalledFunction();
    if (!F || !isCheckingCall(CI->getOperand(0)->getName().c_str())) return false;
    BasicBlock::iterator ptIns(CI);
    ++ptIns;
    CallInst::Create(sFuncWaitForSyncToken, "", ptIns);
    return true;
  }

  bool
  SpeculativeCheckingInsertSyncPoints::isCheckingCall(const std::string & FName) const {
    CheckFuncSetTy::const_iterator it = sCheckFuncSet.find(FName);
    return it != sCheckFuncSet.end();
  }

  /// A "Safe" function means that we don't have to insert
  /// synchronization points before the functions
  // TODO: we might omit the synchronization points of a var-arg 
  // function if all actuals are values
  bool 
  SpeculativeCheckingInsertSyncPoints::isSafeFunction(Function * F) {
    /*
    bool existsPointerArgs = false;
    for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end(); I != E && !existsPointerArgs; ++I) {
      existsPointerArgs = isa<PointerType>(I->getType());
    }
    printf("Function %s is %s\n", F->getName().c_str(), existsPointerArgs ? "unsafe" : "safe");

    return !existsPointerArgs;
    */
    return false;
  }
}
