/// The speculative checking pass lowers synchronous calls to
/// speculative checking calls

#ifndef _SPECULATIVE_CHECKING_H_
#define _SPECULATIVE_CHECKING_H_

#include "llvm/Pass.h"
#include "llvm/Instructions.h"
#include "safecode/Config/config.h"

namespace llvm {
  struct SpeculativeCheckingInsertSyncPoints : public BasicBlockPass {
  public:
    static char ID;
  SpeculativeCheckingInsertSyncPoints() : BasicBlockPass((intptr_t) &ID) {};
    virtual ~SpeculativeCheckingInsertSyncPoints() {};
    virtual bool doInitialization(Module & M);
    virtual bool doInitialization(Function &F) { return false; };
    virtual bool runOnBasicBlock(BasicBlock & BB);
    virtual const char * getPassName() const { return "Insert synchronization points between checking threads and application threads"; };
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {  };

  private:
    bool insertSyncPointsBeforeExternalCall(CallInst * CI);
    bool insertSyncPointsAfterCheckingCall(CallInst * CI);
    bool isSafeFunction(Function * F);
    bool isCheckingCall(const std::string & FName) const;
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
