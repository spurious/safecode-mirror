/// The speculative checking pass lowers synchronous calls to
/// speculative checking calls

#ifndef _SPECULATIVE_CHECKING_H_
#define _SPECULATIVE_CHECKING_H_

#include "llvm/Pass.h"
#include "llvm/Instructions.h"
#include "dsa/CallTargets.h"
#include "dsa/DataStructure.h"
#include "poolalloc/PoolAllocate.h"
#include "safecode/Config/config.h"

#include <map>

#define PAR_CHECKING_ENABLE_INDIRECTCALL_OPT

namespace llvm {
  struct DSNodePass;
  /**
   * This pass analyzes all call instructions in the program and
   * determines which calls are "safe", i.e., calls that can be executed
   * without synchronizing the checking thread.
   *
   * It should be run before pool allocation
   *
   **/   
  struct ParCheckingCallAnalysis : public ModulePass {
    static char ID;
  ParCheckingCallAnalysis() : ModulePass((intptr_t) & ID) {};
    virtual ~ParCheckingCallAnalysis() {}
    virtual const char * getPassName() const { return "Call Safety Analysis for Parallel checking"; }
    virtual bool runOnBasicBlock(BasicBlock & BB);
    virtual bool runOnModule(Module & M);
    virtual void getAnalysisUsage(AnalysisUsage & AU) const {
      AU.addRequired<CallTargetFinder>();
      AU.setPreservesAll();
    }

    /**
     * Whether the call is safe.
     **/
    bool isSafe(CallSite CS) const;

  private:
    std::set<CallSite> CallSafetySet;
    bool isSafeCallSite(CallSite CS) const;
    bool isSafeIndirectCall(CallSite CS) const;
    CallTargetFinder * CTF;
  };

  struct SpeculativeCheckingInsertSyncPoints : public BasicBlockPass {
  public:
    static char ID;
  SpeculativeCheckingInsertSyncPoints() : BasicBlockPass((intptr_t) &ID) {};
    virtual ~SpeculativeCheckingInsertSyncPoints() {};
    virtual bool doInitialization(Module & M);
    virtual bool doInitialization(Function &F) { return false; };
    virtual bool runOnBasicBlock(BasicBlock & BB);
    virtual const char * getPassName() const { return "Insert synchronization points between checking threads and application threads"; };
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
#ifdef PAR_CHECKING_ENABLE_INDIRECTCALL_OPT
      AU.addRequired<EQTDDataStructures>();
      AU.addRequired<PoolAllocateGroup>();
      AU.addRequired<DSNodePass>();
      AU.addRequired<ParCheckingCallAnalysis>();
#endif
       AU.setPreservesAll();
    };

  private:
    bool insertSyncPointsBeforeExternalCall(CallInst * CI);
    void removeRedundantSyncPoints(BasicBlock & BB);
    CallInst * getOriginalCallInst(CallInst * CI);
    DSNodePass * dsnodePass;
    ParCheckingCallAnalysis * callSafetyAnalysis;
  };

  // A pass instruments store instructions to protect the queue
  struct SpeculativeCheckStoreCheckPass : public BasicBlockPass {
  public:
    static char ID;
  SpeculativeCheckStoreCheckPass() : BasicBlockPass((uintptr_t)&ID) {};
    virtual ~SpeculativeCheckStoreCheckPass() {}
    virtual bool doInitialization(Module & M);
    virtual bool doInitialization(Function &F) { return false; };
    virtual const char * getPassName() const { return "Instrument store instructions to protect the metadata of parallel checking"; }
    virtual bool runOnBasicBlock(BasicBlock & BB);
  };
}

#endif
