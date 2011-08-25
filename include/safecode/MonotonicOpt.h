//===- MonotonicOpt.h - Optimize SAFECode checks in loops --------------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file defines a pass that hoists SAFECode run-time checks out of loops.
//
//===----------------------------------------------------------------------===//

#ifndef _SAFECODE_MONOTONICOPT_H_
#define _SAFECODE_MONOTONICOPT_H_

#include "llvm/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"

namespace sc {

struct MonotonicLoopOpt : public LoopPass {
  public:
    static char ID;
    virtual const char *getPassName() const {
      return "Optimize SAFECode checkings in monotonic loops";
    }
    MonotonicLoopOpt() : LoopPass((intptr_t) &ID) {}
    virtual bool doInitialization(Loop *L, LPPassManager &LPM); 
    virtual bool doFinalization(); 
    virtual bool runOnLoop(Loop *L, LPPassManager &LPM);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<TargetData>();
      AU.addRequired<LoopInfo>();
      AU.addRequired<ScalarEvolution>();
      AU.setPreservesCFG();
    }
  private:
    LoopInfo * LI;
    ScalarEvolution * scevPass;
    TargetData * TD;
    bool isMonotonicLoop(Loop * L, Value * loopVar);
    bool isHoistableGEP(GetElementPtrInst * GEP, Loop * L);
    void insertEdgeBoundsCheck(int checkFunctionId, Loop * L, const CallInst * callInst, GetElementPtrInst * origGEP, Instruction *
    ptIns, int type);
    bool optimizeCheck(Loop *L);
    bool isEligibleForOptimization(const Loop * L);
};

}
#endif
