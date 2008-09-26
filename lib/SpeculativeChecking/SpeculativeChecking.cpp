/// This file define a pass to lower synchronous checking calls to
/// speculative checking calls

#include <iostream>
#include <set>
#include <map>
#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"
#include "safecode/Config/config.h"
#include "SpeculativeChecking.h"
#include "VectorListHelper.h"

using namespace llvm;

char SpeculativeCheckingPass::ID = 0;
char SpeculativeCheckingInsertSyncPoints::ID = 0;

static RegisterPass<SpeculativeCheckingPass> passSpeculativeChecking ("speculative-checking", "Lower checkings to speculative checkings");


/// Static Members
namespace {
  typedef std::map<Function *, Function *> CheckFuncMapTy;
  typedef std::set<Function *> CheckFuncSetTy;
  typedef std::set<std::string> SafeFuncSetTy;
  CheckFuncMapTy sCheckFuncMap;
  SafeFuncSetTy sSafeFuncSet;
  CheckFuncSetTy sCheckFuncSet;
  Constant * sFuncWaitForSyncToken;
}

namespace llvm {
cl::opt<bool> OptimisticSyncPoint ("optimistic-sync-point",
                                      cl::init(false),
                                      cl::desc("Place synchronization points only before external functions"));
  ////////////////////////////////////////////////////////////////////////////
  // SpeculativeChecking Methods
  ////////////////////////////////////////////////////////////////////////////

  bool
  SpeculativeCheckingPass::doInitialization(Module & M) {
    static const Type * VoidTy = Type::VoidTy;
    static const Type * vpTy = PointerType::getUnqual(Type::Int8Ty);

    #define REG_FUNC(name, ...) do {					\
	Function * funcOrig = dyn_cast<Function>(M.getOrInsertFunction(name, FunctionType::get(VoidTy, args<const Type*>::list(__VA_ARGS__), false))); \
	Function * funcSpec = dyn_cast<Function>(M.getOrInsertFunction("__sc_" name, FunctionType::get(VoidTy, args<const Type*>::list(__VA_ARGS__), false))); \
      sCheckFuncMap[funcOrig] = funcSpec;				\
      sCheckFuncSet.insert(funcSpec);					\
    } while (0)

    REG_FUNC ("poolcheck",     vpTy, vpTy);
    REG_FUNC ("poolcheckui",   vpTy, vpTy);
    REG_FUNC ("boundscheck",   vpTy, vpTy, vpTy);
    REG_FUNC ("boundscheckui", vpTy, vpTy, vpTy);

#undef REG_FUNC

    sFuncWaitForSyncToken = M.getOrInsertFunction("__sc_wait_for_completion", FunctionType::get(VoidTy, args<const Type*>::list(), false));
    return true;
  }

  bool
  SpeculativeCheckingPass::runOnBasicBlock(BasicBlock & BB) {
    bool changed = false;
    std::set<CallInst *> toBeRemoved;
    for (BasicBlock::iterator I = BB.begin(); I != BB.end(); ++I) {
      if (CallInst * CI = dyn_cast<CallInst>(I)) {
        bool ret = lowerCall(CI);
	if (ret) {
	  toBeRemoved.insert(CI);
	}
	changed |= ret;
      }
    }

    for (std::set<CallInst *>::iterator it = toBeRemoved.begin(), e = toBeRemoved.end(); it != e; ++it) {
      (*it)->eraseFromParent();
    }

    return changed;
  }

  bool
  SpeculativeCheckingPass::lowerCall(CallInst * CI) {
    Function *F = CI->getCalledFunction();
    if (!F) return false;
    CheckFuncMapTy::iterator it = sCheckFuncMap.find(F);
    if (it == sCheckFuncMap.end()) return false;

    BasicBlock::iterator ptIns(CI);
    ++ptIns;
    std::vector<Value *> args;
    for (unsigned i = 1; i < CI->getNumOperands(); ++i) {
      args.push_back(CI->getOperand(i));
    }

    CallInst::Create(it->second, args.begin(), args.end(), "", ptIns);
    return true;
  }

  ////////////////////////////////////////////////////////////////////////////
  // SpeculativeCheckingInsertSyncPoints Methods
  ////////////////////////////////////////////////////////////////////////////

  bool
  SpeculativeCheckingInsertSyncPoints::doInitialization(Module & M) {
    sFuncWaitForSyncToken = 
      M.getOrInsertFunction("__sc_wait_for_completion", 
			    FunctionType::get(Type::VoidTy, 
				      args<const Type*>::list(), false)
			    );
    const char * safeFuncList[]
      = {
      "poolinit", "pool_init_runtime", "poolregister", "poolunregister",
      "exactcheck", "exactcheck2"
    };

    for (size_t i = 0; i < sizeof(safeFuncList) / sizeof(const char *); ++i) {
      sSafeFuncSet.insert(safeFuncList[i]);
    }
    return true;
  }

  bool
  SpeculativeCheckingInsertSyncPoints::runOnBasicBlock(BasicBlock & BB) {
    bool changed = false;
    typedef bool (SpeculativeCheckingInsertSyncPoints::*HandlerTy)(CallInst*);
    HandlerTy handler = OptimisticSyncPoint ? 
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
    if (F && isSafeFunction(F)) return false;
    if (F && !(F->isDeclaration())) return false;

    // TODO: Skip some intrinsic, like pow / exp

    CallInst::Create(sFuncWaitForSyncToken, "", CI);
    return true;
  }

  bool
  SpeculativeCheckingInsertSyncPoints::insertSyncPointsAfterCheckingCall(CallInst * CI) {
    Function *F = CI->getCalledFunction();
    if (!F || !isCheckingCall(F)) return false;
    BasicBlock::iterator ptIns(CI);
    ++ptIns;
    CallInst::Create(sFuncWaitForSyncToken, "", ptIns);
    return true;
  }

  bool
  SpeculativeCheckingInsertSyncPoints::isCheckingCall(Function *F) {
    CheckFuncSetTy::const_iterator it = sCheckFuncSet.find(F);
    return it != sCheckFuncSet.end();
  }

  /// A "Safe" function means that we don't have to insert
  /// synchronization points before the functions
  // TODO: we might omit the synchronization points of a var-arg 
  // function if all actuals are values
  bool 
  SpeculativeCheckingInsertSyncPoints::isSafeFunction(Function * F) {
    if (isCheckingCall(F)) return true;
    SafeFuncSetTy::const_iterator it = sSafeFuncSet.find(F->getName());
    if(it != sSafeFuncSet.end()) return true;

    bool existsPointerArgs = false;
    for (Function::arg_iterator I = F->arg_begin(), E = F->arg_end(); I != E && !existsPointerArgs; ++I) {
      existsPointerArgs = isa<PointerType>(I->getType());
    }
    return !existsPointerArgs;
  }
}
