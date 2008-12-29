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

#ifndef LLVA_KERNEL
#include "SafeDynMemAlloc.h"
#include "poolalloc/PoolAllocate.h"
#endif

NAMESPACE_SC_BEGIN

using namespace CUA;

struct PreInsertPoolChecks : public ModulePass {
    friend struct InsertPoolChecks;
    private :
    // Flags whether we want to do dangling checks
    bool DanglingChecks;

    // Flags whether we should rewrite Out of Bound pointers or just fail them
    bool RewriteOOB;

    public :
    static char ID;
    PreInsertPoolChecks (bool DPChecks = false, bool RewritePtrs=false)
      : ModulePass ((intptr_t) &ID) {
      DanglingChecks = DPChecks;
      RewriteOOB = RewritePtrs;
    }
    const char *getPassName() const { return "Register Global variable into pools"; }
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<InsertSCIntrinsic>();
      AU.addPreserved<InsertSCIntrinsic>();
#ifndef LLVA_KERNEL      
      AU.addRequired<TargetData>();
#endif
      AU.addRequiredTransitive<PoolAllocateGroup>();
      AU.addRequiredTransitive<EQTDDataStructures>();
      AU.addRequired<DSNodePass>();

      AU.addPreserved<EQTDDataStructures>();
      AU.addPreserved<PoolAllocateGroup>();
      AU.addPreserved<DSNodePass>();
      AU.setPreservesCFG();
    };
    private :
    InsertSCIntrinsic * intrinsic;
#ifndef  LLVA_KERNEL
  PoolAllocateGroup * paPass;
  TargetData * TD;
#endif
  DSNodePass * dsnPass;
#ifndef LLVA_KERNEL  
  void registerGlobalArraysWithGlobalPools(Module &M);
#endif  
  void insertInitCalls (Module & M, bool DanglingChecks, bool RewriteOOB);
};

struct InsertPoolChecks : public FunctionPass {
    public :
    static char ID;
    InsertPoolChecks () : FunctionPass ((intptr_t) &ID) { }
    const char *getPassName() const { return "Inserting Pool checks Pass"; }
    virtual bool doFinalization(Module &M);
    virtual bool runOnFunction(Function &F);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
//      AU.addRequired<EQTDDataStructures>();
//      AU.addRequired<TDDataStructures>();
#ifndef LLVA_KERNEL      
      AU.addRequired<ArrayBoundsCheck>();
      AU.addRequired<TargetData>();
#else 
      AU.addRequired<TDDataStructures>();
#endif
      AU.addRequired<InsertSCIntrinsic>();
      AU.addRequiredTransitive<PoolAllocateGroup>();
      AU.addRequired<DSNodePass>();

      AU.addPreserved<InsertSCIntrinsic>();
      AU.addPreserved<EQTDDataStructures>();
      AU.addPreserved<PoolAllocateGroup>();
      AU.addPreserved<DSNodePass>();
      AU.setPreservesCFG();
    };
    private:
    InsertSCIntrinsic * intrinsic;
      ArrayBoundsCheck * abcPass;
#ifndef  LLVA_KERNEL
  PoolAllocateGroup * paPass;
  TargetData * TD;
#else
  TDDataStructures * TDPass;
#endif  
  DSNodePass * dsnPass;
  Function *PoolCheck;
  Function *PoolCheckUI;
  Function *PoolCheckAlign;
  Function *PoolCheckAlignUI;
  Function *PoolCheckArray;
  Function *PoolCheckArrayUI;
  Function *ExactCheck;
  Function *ExactCheck2;
  Function *FunctionCheck;
  Function *GetActualValue;
  void addCheckProto(Module &M);
  void addPoolChecks(Function &F);
  void addGetElementPtrChecks(BasicBlock * BB);
  void addGetActualValue(llvm::ICmpInst*, unsigned int);
  bool insertExactCheck (GetElementPtrInst * GEP);
#if 0
  bool insertExactCheck (Instruction * , Value *, Value *, Instruction *);
  void addExactCheck (Value * P, Value * I, Value * B, Instruction * InsertPt);
#endif
  void addLoadStoreChecks(Function &F);
  void addExactCheck2 (Value * B, Value * R, Value * C, Instruction * InsertPt);
  void insertAlignmentCheck (LoadInst * LI);
#ifndef LLVA_KERNEL  
  void addLSChecks(Value *Vnew, const Value *V, Instruction *I, Function *F);
  void registerGlobalArraysWithGlobalPools(Module &M);
#else
  void addLSChecks(Value *V, Instruction *I, Function *F);
#endif  
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
    AU.addRequired<LoopInfo>();
    AU.addRequired<ScalarEvolution>();
    AU.addPreserved<EQTDDataStructures>();
    AU.addPreserved<PoolAllocateGroup>();
    AU.addPreserved<DSNodePass>();
    AU.setPreservesCFG();
  }
  private:
  LoopInfo * LI;
  ScalarEvolution * scevPass;
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
  virtual bool doInitialization(Module & M);
  virtual bool runOnFunction(Function &F);
  virtual const char * getPassName() const { return "Register stack variables into pool"; }
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequiredTransitive<PoolAllocateGroup>();
    AU.addRequiredTransitive<EQTDDataStructures>();
    AU.addRequiredTransitive<DSNodePass>();

    AU.addRequired<DominatorTree>();
    AU.addRequired<TargetData>();

    AU.setPreservesAll();
  };

  private:
    PoolAllocateGroup * paPass;
    TargetData * TD;
    DSNodePass * dsnPass;
    DominatorTree * DT;
    void registerAllocaInst(AllocaInst *AI, AllocaInst *AIOrig, DomTreeNode * DTN);
 };

 extern ModulePass * createClearCheckAttributesPass();

NAMESPACE_SC_END
#endif
