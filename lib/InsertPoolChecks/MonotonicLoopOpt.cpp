//===- MonotonicLoopOpt.cpp: Monotonic Loop Optimizations for SAFECode ----===//
//                          The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass eliminates redundant checks in monotonic loops.
// FIXME: This pass is broken right now due to LLVM API changes
//
//===----------------------------------------------------------------------===//

#include "safecode/SAFECode.h"
#include <set>
#include "SCUtils.h"
#include "safecode/InsertChecks.h"
#include "llvm/Analysis/ScalarEvolutionExpander.h"
#include "llvm/ADT/Statistic.h"

using namespace llvm;

NAMESPACE_SC_BEGIN

namespace {
  RegisterPass<MonotonicLoopOpt>
  X("sc-monotonic-loop-opt", "Monotonic Loop Optimization for SAFECode", true);

  STATISTIC (MonotonicLoopOptPoolCheck,
       "Number of monotonic loop optimization performed for poolcheck");
  STATISTIC (MonotonicLoopOptPoolCheckUI,
       "Number of monotonic loop optimization performed for poolcheckUI");
  STATISTIC (MonotonicLoopOptPoolCheckAlign,
       "Number of monotonic loop optimization performed for poolcheckalign");
  STATISTIC (MonotonicLoopOptExactCheck,
       "Number of monotonic loop optimization performed for exactcheck");
  STATISTIC (MonotonicLoopOptExactCheck2,
       "Number of monotonic loop optimization performed for exactcheck2");
  STATISTIC (MonotonicLoopOptBoundsCheck,
       "Number of monotonic loop optimization performed for boundscheck");
  STATISTIC (MonotonicLoopOptBoundsCheckUI,
       "Number of monotonic loop optimization performed for boundscheckUI");

  enum {
    CHECK_FUNC_POOLCHECK = 0,
    CHECK_FUNC_POOLCHECKUI,
    CHECK_FUNC_POOLCHECKALIGN,
    CHECK_FUNC_EXACTCHECK,
    CHECK_FUNC_EXACTCHECK2,
    CHECK_FUNC_BOUNDSCHECK,
    CHECK_FUNC_BOUNDSCHECKUI,
    CHECK_FUNC_COUNT
  };
  
  static llvm::Statistic * statData[] = {
    &MonotonicLoopOptPoolCheck,
    &MonotonicLoopOptPoolCheckUI,
    &MonotonicLoopOptPoolCheckAlign,
    &MonotonicLoopOptExactCheck,
    &MonotonicLoopOptExactCheck2,
    &MonotonicLoopOptBoundsCheck,
    &MonotonicLoopOptBoundsCheckUI,
  };

  struct checkFunctionInfo {
    const int id; 
    const std::string name;
    // The operand position in the checking function, 0 means not applicable
    const int argPoolHandlePos;
    const int argSrcPtrPos;
    const int argDestPtrPos;
    explicit checkFunctionInfo(int id,
                               const char * name,
                               int argPoolHandlePos,
                               int argSrcPtrPos,
                               int argDestPtrPos) :
     id(id), name(name), argPoolHandlePos(argPoolHandlePos),
     argSrcPtrPos(argSrcPtrPos), argDestPtrPos(argDestPtrPos) {}
  };
  
  static const checkFunctionInfo checkFunctions[] = {
    checkFunctionInfo(CHECK_FUNC_POOLCHECK,     "poolcheck",     1, 0, 2),
    checkFunctionInfo(CHECK_FUNC_POOLCHECKUI,   "poolcheckui",   1, 0, 2),
    checkFunctionInfo(CHECK_FUNC_POOLCHECKALIGN,"poolcheckalign",1, 0, 2),
    checkFunctionInfo(CHECK_FUNC_EXACTCHECK,    "exactcheck",    0, 0, 3),
    checkFunctionInfo(CHECK_FUNC_EXACTCHECK2,   "exactcheck2",   0, 1, 2),
    checkFunctionInfo(CHECK_FUNC_BOUNDSCHECK,   "boundscheck",   0, 2, 3),
    checkFunctionInfo(CHECK_FUNC_BOUNDSCHECKUI, "boundscheckui", 0, 2, 3)
  };

  typedef std::map<std::string, int> checkFuncMapType;
  checkFuncMapType checkFuncMap;
  


  //
  // Function: getGEPFromCheckCallInst()
  //
  // Description:
  //  Try to find the GEP from the call instruction of the checking function.
  //
  static GetElementPtrInst *
  getGEPFromCheckCallInst(int checkFunctionId, CallInst * callInst) {
    const checkFunctionInfo & info = checkFunctions[checkFunctionId];
    Value * inst = callInst->getOperand(info.argDestPtrPos);
    if (isa<GetElementPtrInst>(inst)) {
      return dyn_cast<GetElementPtrInst>(inst);
    } else if (isa<BitCastInst>(inst)) {
      return dyn_cast<GetElementPtrInst>
        (dyn_cast<BitCastInst>(inst)->getOperand(0));
    }
    return NULL;
  }
  static std::set<Loop*> optimizedLoops;
}

  char MonotonicLoopOpt::ID = 0;

  /// Find the induction variable for a loop
  /// Based on include/llvm/Analysis/LoopInfo.h
  static bool getPossibleLoopVariable(Loop * L, std::vector<PHINode*> & list) {
    list.clear();
    BasicBlock *H = L->getHeader();

    BasicBlock *Incoming = 0, *Backedge = 0;
    typedef GraphTraits<Inverse<BasicBlock*> > InvBasicBlockraits;
    InvBasicBlockraits::ChildIteratorType PI = InvBasicBlockraits::child_begin(H);
    assert(PI != InvBasicBlockraits::child_end(H) &&
     "Loop must have at least one backedge!");
    Backedge = *PI++;
    if (PI == InvBasicBlockraits::child_end(H)) return 0;  // dead loop
    Incoming = *PI++;
    if (PI != InvBasicBlockraits::child_end(H)) return 0;  // multiple backedges?
    // FIXME: Check incoming edges

    if (L->contains(Incoming)) {
      if (L->contains(Backedge))
        return 0;
      std::swap(Incoming, Backedge);
    } else if (!L->contains(Backedge))
      return 0;

    // Loop over all of the PHI nodes, looking for a canonical indvar.
    for (BasicBlock::iterator I = H->begin(), E=H->end(); I != E;  ++I) {
      isa<PHINode>(I);
      PHINode *PN = dyn_cast<PHINode>(I);
      if (PN) {
        list.push_back(PN);
      }
    }
    return list.size() > 0;
  }
  
  bool
  MonotonicLoopOpt::doFinalization() { 
    optimizedLoops.clear();
    return false;
  }

  // Initialization for the check function name -> check function id
  bool
  MonotonicLoopOpt::doInitialization(Loop *L, LPPassManager &LPM) { 
    optimizedLoops.clear();
    for (size_t i = 0; i < CHECK_FUNC_COUNT; ++i) {
      checkFuncMap[checkFunctions[i].name] = checkFunctions[i].id;
    }
    return false;
  }

  //
  // Method: isMonotonicLoop()
  //
  // Description:
  //  Determines whether the given loop is monotonic and, if so, whether the
  //  starting and ending values of the loop variable can be computed.
  //
  // Inputs:
  //  L       - The loop to verify.
  //  loopVar - The loop induction variable.
  //
  // Return value:
  //  true  - The loop is monotonic and the start and end values of the loop
  //          induction variable can be determined.
  //  false - The loop is not monotonic and/or the start and/or end values of
  //          the loop induction variable cannot be determined.
  //
  bool
  MonotonicLoopOpt::isMonotonicLoop(Loop * L, Value * loopVar) {
    //
    // Determine whether the loop has a constant iteration count.
    //
    bool HasConstantItCount = false;
    if (scevPass->hasLoopInvariantBackedgeTakenCount(L))
      HasConstantItCount=isa<SCEVConstant>(scevPass->getBackedgeTakenCount(L));

    //
    // Determine whether ScalarEvolution can provide information on the loop
    // induction variable.  If it cannot, then just assume that the loop is
    // non-monotonic.
    //
    if (!(scevPass->isSCEVable(loopVar->getType())))
      return false;

    const SCEV * SH = scevPass->getSCEV(loopVar);
    if (SH->hasComputableLoopEvolution(L) ||    // Varies predictably
        HasConstantItCount) {
      const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(SH);
      if (AR && AR->isAffine()) {
        const SCEV * startVal = AR->getStart();
        const SCEV * endVal = scevPass->getSCEVAtScope(loopVar, L->getParentLoop());
        if (!isa<SCEVCouldNotCompute>(startVal) && !isa<SCEVCouldNotCompute>(endVal)){
          // Success
          return true;
        }
      }
    }
    // It does not seem like a monotonic one.
    return false;
  }
 
  /// Determines if a GEP can be hoisted
  bool 
  MonotonicLoopOpt::isHoistableGEP(GetElementPtrInst * GEP, Loop * L) {
    for(int i = 0, end = GEP->getNumOperands(); i != end; ++i) {
      Value * op = GEP->getOperand(i);
      if (L->isLoopInvariant(op)) continue;

      const SCEV * SH = scevPass->getSCEV(op);
      if (!SH->hasComputableLoopEvolution(L)) return false;
      const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(SH);
      if (!AR || !AR->isAffine()) return false;
      const SCEV * startVal = AR->getStart();
      const SCEV * endVal = scevPass->getSCEVAtScope(op, L->getParentLoop());
      if (isa<SCEVCouldNotCompute>(startVal) ||
          isa<SCEVCouldNotCompute>(endVal)) {
        return false;
      }
    }
    return true;
  }

  /// Insert checks for edge condition

  void
  MonotonicLoopOpt::insertEdgeBoundsCheck (int checkFunctionId,
                                           Loop * L,
                                           const CallInst * callInst,
                                           GetElementPtrInst * origGEP,
                                           Instruction * ptIns,
                                           int type) {
    enum {
      LOWER_BOUND,
      UPPER_BOUND
    };
    
    static const char * suffixes[] = {".lower", ".upper"};

    SCEVExpander Rewriter(*scevPass); 
    
    Instruction *newGEP = origGEP->clone();
    newGEP->setName(origGEP->getName() + suffixes[type]);
    for(int i = 0, end = origGEP->getNumOperands(); i != end; ++i) {
      Value * op = origGEP->getOperand(i);
      if (L->isLoopInvariant(op)) continue;
      
      const SCEV * SH = scevPass->getSCEV(op);
      const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(SH);
      const SCEV * startVal = AR->getStart();
      const SCEV * endVal = scevPass->getSCEVAtScope(op, L->getParentLoop());
      const SCEV * & val = type == LOWER_BOUND ? startVal : endVal; 
      Value * boundsVal = Rewriter.expandCodeFor(val, val->getType(), ptIns);
      newGEP->setOperand(i, boundsVal);
    }
    
    newGEP->insertBefore(ptIns);
   
    const Type * Int8Type  = IntegerType::getInt8Ty(getGlobalContext());
    CastInst * castedNewGEP = CastInst::CreatePointerCast(newGEP,
                PointerType::getUnqual(Int8Type), newGEP->getName() + ".casted",
                ptIns);

    Instruction * checkInst = callInst->clone();
    const checkFunctionInfo & info = checkFunctions[checkFunctionId];

    if (info.argSrcPtrPos) {
      // Copy the srcPtr if necessary
      CastInst * newSrcPtr = CastInst::CreatePointerCast
        (origGEP->getPointerOperand(),
          PointerType::getUnqual(Int8Type), origGEP->getName() + ".casted",
          newGEP);
      checkInst->setOperand(info.argSrcPtrPos, newSrcPtr);
    }
    
    if (info.argPoolHandlePos) {
      // Copy the pool handle if necessary
      Instruction * newPH = cast<Instruction>(checkInst->getOperand(1))->clone();
      newPH->insertBefore(ptIns);
      checkInst->setOperand(info.argPoolHandlePos, newPH);
    }
    
    checkInst->setOperand(info.argDestPtrPos, castedNewGEP);
    checkInst->insertBefore(ptIns);
  }
  

  bool
  MonotonicLoopOpt::runOnLoop(Loop *L, LPPassManager &LPM) {
    LI = &getAnalysis<LoopInfo>();
    scevPass = &getAnalysis<ScalarEvolution>();
    TD = &getAnalysis<TargetData>();

    for (Loop::iterator LoopItr = L->begin(), LoopItrE = L->end();
         LoopItr != LoopItrE; ++LoopItr) {
      if (optimizedLoops.find(*LoopItr) == optimizedLoops.end())
        {
          // Handle sub loops first
          LPM.redoLoop(L);
          return false;
        }
    }
    optimizedLoops.insert(L);
    return optimizeCheck(L);
  }

  bool
  MonotonicLoopOpt::optimizeCheck(Loop *L) {
    bool changed = false;
    if (!isEligibleForOptimization(L)) return false;
    // Get the preheader block to move instructions into...
    BasicBlock * Preheader = L->getLoopPreheader();
      
    std::vector<PHINode *> loopVarList;
    getPossibleLoopVariable(L, loopVarList);
    PHINode * loopVar = NULL;
    for (std::vector<PHINode*>::iterator it = loopVarList.begin(), end = loopVarList.end(); it != end; ++it) {
      if (!isMonotonicLoop(L, *it)) continue;
      loopVar = *it;

      // Loop over the body of this loop, looking for calls, invokes, and stores.
      // Because subloops have already been incorporated into AST, we skip blocks in
      // subloops.
      //
      std::vector<CallInst*> toBeRemoved;
      for (Loop::block_iterator I = L->block_begin(), E = L->block_end();
           I != E; ++I) {
        BasicBlock *BB = *I;
        if (LI->getLoopFor(BB) != L) continue; // Ignore blocks in subloops...

        for (BasicBlock::iterator it = BB->begin(), end = BB->end(); it != end;
             ++it) {
          CallInst * callInst = dyn_cast<CallInst>(it);
          if (!callInst) continue;

          Function * F = callInst->getCalledFunction();
          if (!F) continue;

          checkFuncMapType::iterator it = checkFuncMap.find(F->getName());
          if (it == checkFuncMap.end()) continue;

          int checkFunctionId = it->second;
          GetElementPtrInst * GEP = getGEPFromCheckCallInst(checkFunctionId, callInst);

          if (!GEP || !isHoistableGEP(GEP, L)) continue;
                
          Instruction *ptIns = Preheader->getTerminator();

          insertEdgeBoundsCheck(checkFunctionId, L, callInst, GEP, ptIns, 0);
          insertEdgeBoundsCheck(checkFunctionId, L, callInst, GEP, ptIns, 1);
          toBeRemoved.push_back(callInst);

          ++(*(statData[checkFunctionId]));
          changed = true;
        }

      }
      for (std::vector<CallInst*>::iterator it = toBeRemoved.begin(), end = toBeRemoved.end(); it != end; ++it) {
        (*it)->eraseFromParent();
      }
    }
    return changed;
  }

  /// Test whether a loop is eligible for monotonic optmization
  /// A loop should satisfy all these following conditions before optmization:
  /// 1. Have an preheader
  /// 2. There is only *one* exitblock in the loop
  /// 3. There is no other instructions (actually we only handle call
  ///    instruction) in the loop change the bounds of the check
  /// TODO: we should run a bottom-up call graph analysis to identify the 
  /// calls that are SAFE, i.e., calls that do not affect the bounds of arrays.
  ///
  /// Currently we scan through the loop (including sub-loops), we
  /// don't do the optimization if there exists a call instruction in
  /// the loop.

  bool
  MonotonicLoopOpt::isEligibleForOptimization(const Loop * L) {
    BasicBlock * Preheader = L->getLoopPreheader();
    if (!Preheader) return false;
    
    SmallVector<BasicBlock*, 4> exitBlocks;
    L->getExitingBlocks(exitBlocks);
    if (exitBlocks.size() != 1) {
      return false;
    }


    for (Loop::block_iterator I = L->block_begin(), E = L->block_end();
         I != E; ++I) {
      BasicBlock *BB = *I;
      for (BasicBlock::iterator I = BB->begin(), E = BB->end(); I != E; ++I) {
        if (CallInst * CI = dyn_cast<CallInst>(I)) {
          Function * F = CI->getCalledFunction();
          if (F && isCheckingCall(F->getName())) return false;
        }
      }
    }     
    return true;
  }

NAMESPACE_SC_END
