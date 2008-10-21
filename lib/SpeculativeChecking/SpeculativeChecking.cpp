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
#include "InsertPoolChecks.h"

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
  "memset", "memcmp"
  "llvm.memcpy.i32", "llvm.memcpy.i64",
  "llvm.memset.i32", "llvm.memset.i64",
  "llvm.memmove.i32", "llvm.memmove.i64",
  "llvm.sqrt.f64",
  // These functions are not marked as "readonly"
  // So we have to add them to the list explicitly
  "atoi", "srand", "fabs", "random", "srandom", "drand48"

};

// Functions used in checking
static const char * checkingFunctions[] = {
  "exactcheck", "exactcheck2", "funccheck",
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
#ifdef PAR_CHECKING_ENABLE_INDIRECTCALL_OPT
    CTF = &getAnalysis<CallTargetFinder>();
#endif
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

  bool
  SpeculativeCheckingInsertSyncPoints::insertSyncPointsBeforeExternalCall(CallInst * CI) {
    Function *F = CI->getCalledFunction();
    Value * Fop = CI->getOperand(0);
    const std::string & FName = Fop->getName();
    bool checkingCall = isCheckingCall(FName);
    sHaveSeenCheckingCall |= checkingCall; 
    
    if (isSafeDirectCall(F)) return false;

#ifdef PAR_CHECKING_ENABLE_INDIRECTCALL_OPT
    // indirect function call 
    if (!F && isSafeIndirectCall(CI)) return false; 
    // TODO: Skip some intrinsic, like pow / exp
#endif

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

  bool SpeculativeCheckingInsertSyncPoints::isSafeDirectCall(const Function * F) const {
    if (!F) return false;
    const std::string & FName = F->getName();
    
    // in the exception list?
    SafeFuncSetTy::const_iterator it = sSafeFuncSet.find(FName);
    if (it != sSafeFuncSet.end() || isCheckingCall(FName)) return true;
    
    if (!F->isDeclaration()) return true;
    if (F->onlyReadsMemory()) return true;
    return false;
  } 

  bool SpeculativeCheckingInsertSyncPoints::isSafeIndirectCall(CallInst * CI) const {
     CallSite CS = CallSite::get(CI);
     if (!CTF->isComplete(CS)) return false;

     typedef std::vector<const Function*>::iterator iter_t;
     for (iter_t it = CTF->begin(CS), end = CTF->end(CS); it != end; ++it) {
       if (!isSafeDirectCall(*it)) {
          return false;
       }
     }
     return true;
  }

  ///
  /// SpeculativeCheckStoreCheckPass methods
  ///
  char SpeculativeCheckStoreCheckPass::ID = 0;
  static Constant * funcStoreCheck;

  bool SpeculativeCheckStoreCheckPass::doInitialization(Module & M) {
    std::vector<const Type *> args;
    args.push_back(PointerType::getUnqual(Type::Int8Ty));
    FunctionType * funcStoreCheckTy = FunctionType::get(Type::VoidTy, args, false);
    funcStoreCheck = M.getOrInsertFunction("__sc_par_store_check", funcStoreCheckTy);
    return true;
  }

  // TODO: Handle volatile instructions
  bool SpeculativeCheckStoreCheckPass::runOnBasicBlock(BasicBlock & BB) {
    bool changed = false;    
    for (BasicBlock::iterator I = BB.begin(), E = BB.end(); I != E; ++I) {
      if (StoreInst * SI = dyn_cast<StoreInst>(I)) {
	Instruction * CastedPointer = CastInst::CreatePointerCast(SI->getPointerOperand(), PointerType::getUnqual(Type::Int8Ty), "", SI);
	
	CallInst::Create(funcStoreCheck, CastedPointer, "", SI);
	changed = true;
      }
    }
    return changed;
  }

}
