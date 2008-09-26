/// The speculative checking pass lowers synchronous calls to
/// speculative checking calls

#ifndef _SPECULATIVE_CHECKING_H_
#define _SPECULATIVE_CHECKING_H_

#include "llvm/Pass.h"
#include "llvm/Instructions.h"
#include "safecode/Config/config.h"

namespace llvm {

  struct SpeculativeCheckingPass : public BasicBlockPass {
  public:
    static char ID;
  SpeculativeCheckingPass() : BasicBlockPass((intptr_t) &ID) {};
    virtual ~SpeculativeCheckingPass() {};
    virtual bool doInitialization(Module & M);
    virtual bool doInitialization(Function &F) { return false; };
    virtual bool runOnBasicBlock(BasicBlock & BB);
    virtual const char * getPassName() const { return "Lower checkings to speculative chekings";}
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {  };

  private:
    bool lowerCall(CallInst * CI);
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
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {  };

  private:
    bool insertSyncPointsBeforeExternalCall(CallInst * CI);
    bool insertSyncPointsAfterCheckingCall(CallInst * CI);
    bool isSafeFunction(Function * F);
    bool isCheckingCall(Function * F);
  };

}

#endif
