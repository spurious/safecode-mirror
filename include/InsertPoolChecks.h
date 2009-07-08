//===- InsertPoolChecks.h - Insert run-time checks for SAFECode --------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements several passes that insert run-time checks to enforce
// SAFECode's memory safety guarantees as well as several other passes that
// help to optimize the instrumentation.
//
//===----------------------------------------------------------------------===//

#ifndef _INSERT_POOLCHECKS_H_
#define _INSERT_POOLCHECKS_H_

#include "safecode/SAFECode.h"
#include "llvm/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "safecode/PoolHandles.h"
#include "ArrayBoundsCheck.h"
#include "ConvertUnsafeAllocas.h"
#include "safecode/Intrinsic.h"

#include "SafeDynMemAlloc.h"
#include "poolalloc/PoolAllocate.h"

extern bool isSVAEnabled();

NAMESPACE_SC_BEGIN

using namespace CUA;

struct InsertPoolChecks : public FunctionPass {
    public :
    static char ID;
    InsertPoolChecks () : FunctionPass ((intptr_t) &ID) { }
    const char *getPassName() const { return "Inserting Pool checks Pass"; }
    virtual bool doFinalization(Module &M);
    virtual bool runOnFunction(Function &F);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<ArrayBoundsCheckGroup>();
      AU.addRequired<TargetData>();
      AU.addRequired<InsertSCIntrinsic>();
      AU.addRequired<DSNodePass>();
			//lying!
      DSNodePass::preservePAandDSA(AU);
      AU.addPreserved<InsertSCIntrinsic>();
      AU.addPreserved<DSNodePass>();
      AU.setPreservesCFG();
    };
    private:
    InsertSCIntrinsic * intrinsic;
    ArrayBoundsCheckGroup * abcPass;
  PoolAllocateGroup * paPass;
  TargetData * TD;
  DSNodePass * dsnPass;
  Function *PoolCheck;
  Function *PoolCheckUI;
  Function *PoolCheckAlign;
  Function *PoolCheckAlignUI;
  Function *PoolCheckArray;
  Function *PoolCheckArrayUI;
  Function *FunctionCheck;
	void addCheckProto(Module &M);
  void addPoolChecks(Function &F);
  void addGetElementPtrChecks(GetElementPtrInst * GEP);
  void addLoadStoreChecks(Function &F);
  void insertAlignmentCheck (LoadInst * LI);
  void addLSChecks(Value *Vnew, const Value *V, Instruction *I, Function *F);
};

/// Monotonic Loop Optimization
struct MonotonicLoopOpt : public LoopPass {
  static char ID;
  virtual const char *getPassName() const { return "Optimize SAFECode checkings in monotonic loops"; }
  MonotonicLoopOpt() : LoopPass((intptr_t) &ID) {}
  virtual bool doInitialization(Loop *L, LPPassManager &LPM); 
  virtual bool doFinalization(); 
  virtual bool runOnLoop(Loop *L, LPPassManager &LPM);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<TargetData>();
    AU.addRequired<LoopInfo>();
    AU.addRequired<ScalarEvolution>();
		// lying !
		DSNodePass::preservePAandDSA(AU);
    AU.addPreserved<DSNodePass>();
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

struct RegisterStackObjPass : public FunctionPass {
  public:
    static char ID;
    RegisterStackObjPass() : FunctionPass((intptr_t) &ID) {};
    virtual ~RegisterStackObjPass() {};
    virtual bool runOnFunction(Function &F);
    virtual const char * getPassName() const {
      return "Register stack variables into pool";
    }
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequiredTransitive<DSNodePass>();

      AU.addRequired<TargetData>();
      AU.addRequired<LoopInfo>();
      AU.addRequired<InsertSCIntrinsic>();

      //
      // Claim that we preserve the DSA and pool allocation results since they
      // are needed by subsequent SAFECode passes.
      //
      // TODO: Determine whether this code is really lying or not.
      //
      DSNodePass::preservePAandDSA(AU);
      AU.addPreserved<InsertSCIntrinsic>();
      AU.setPreservesAll();
    };

  private:
    // References to other LLVM passes
    PoolAllocateGroup * paPass;
    TargetData * TD;
    LoopInfo * LI;
    DSNodePass * dsnPass;
    DominatorTree * DT;
    InsertSCIntrinsic * intrinsic;

    // The pool registration function
    Constant *PoolRegister;

    CallInst * registerAllocaInst(AllocaInst *AI);
 };

 extern ModulePass * createClearCheckAttributesPass();

NAMESPACE_SC_END
#endif
