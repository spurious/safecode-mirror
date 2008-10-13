#define INSERT_BOUNDS_H

#include "llvm/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Analysis/LoopPass.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "safecode/Config/config.h"
#include "ArrayBoundsCheck.h"
#include "ConvertUnsafeAllocas.h"

#ifndef LLVA_KERNEL
#include "SafeDynMemAlloc.h"
#include "poolalloc/PoolAllocate.h"
#endif

namespace llvm {

using namespace CUA;

struct DSNodePass;

struct PreInsertPoolChecks : public ModulePass {
    friend struct InsertPoolChecks;
    private :
    // Flags whether we want to do dangling checks
    bool DanglingChecks;

    public :
    static char ID;
    PreInsertPoolChecks (bool DPChecks = false)
      : ModulePass ((intptr_t) &ID) {
      DanglingChecks = DPChecks;
    }
    const char *getPassName() const { return "Register Global variable into pools"; }
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
#ifndef LLVA_KERNEL      
      AU.addRequired<TargetData>();
#endif
      AU.addRequiredTransitive<PoolAllocateGroup>();
      AU.addPreserved<PoolAllocateGroup>();
      AU.addRequired<DSNodePass>();
      AU.addPreserved<DSNodePass>();
      AU.setPreservesCFG();
    };
    private :
#ifndef  LLVA_KERNEL
  PoolAllocateGroup * paPass;
  TargetData * TD;
#endif
  DSNodePass * dsnPass;
  Constant *RuntimeInit;
#ifndef LLVA_KERNEL  
  void registerGlobalArraysWithGlobalPools(Module &M);
#endif  
};

struct InsertPoolChecks : public FunctionPass {
    public :
    static char ID;
    InsertPoolChecks () : FunctionPass ((intptr_t) &ID) { }
    const char *getPassName() const { return "Inserting Pool checks Pass"; }
    virtual bool doInitialization(Module &M);
    virtual bool doFinalization(Module &M);
    virtual bool runOnFunction(Function &F);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
//      AU.addRequired<CompleteBUDataStructures>();
//      AU.addRequired<TDDataStructures>();
#ifndef LLVA_KERNEL      
      AU.addRequired<ArrayBoundsCheck>();
      AU.addRequired<TargetData>();
#else 
      AU.addRequired<TDDataStructures>();
#endif
      AU.addRequiredTransitive<PoolAllocateGroup>();
      AU.addPreserved<PoolAllocateGroup>();
      AU.addRequired<DSNodePass>();
	  AU.addPreserved<DSNodePass>();
	  AU.setPreservesCFG();
    };
    private :
      ArrayBoundsCheck * abcPass;
#ifndef  LLVA_KERNEL
  PoolAllocateGroup * paPass;
  TargetData * TD;
#else
  TDDataStructures * TDPass;
#endif  
  DSNodePass * dsnPass;
  Constant *PoolCheck;
  Constant *PoolCheckUI;
  Constant *PoolCheckAlign;
  Constant *PoolCheckAlignUI;
  Constant *PoolCheckArray;
  Constant *PoolCheckArrayUI;
  Constant *ExactCheck;
  Constant *ExactCheck2;
  Constant *FunctionCheck;
  Constant *GetActualValue;
  void addCheckProto(Module &M);
  void addPoolChecks(Function &F);
  void addGetElementPtrChecks(BasicBlock * BB);
  void addGetActualValue(llvm::ICmpInst*, unsigned int);
  bool insertExactCheck (GetElementPtrInst * GEP);
  bool insertExactCheck (Instruction * , Value *, Value *, Instruction *);
  void addLoadStoreChecks(Function &F);
  void addExactCheck (Value * P, Value * I, Value * B, Instruction * InsertPt);
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
  const char *getPassName() const { return "Optimize SAFECode checkings in monotonic loops"; }
  MonotonicLoopOpt() : LoopPass((intptr_t) &ID) {}
  virtual bool doInitialization(Loop *L, LPPassManager &LPM); 
  virtual bool doFinalization(); 
  virtual bool runOnLoop(Loop *L, LPPassManager &LPM);
  virtual void getAnalysisUsage(AnalysisUsage &AU) const {
    AU.addRequired<LoopInfo>();
    AU.addRequired<ScalarEvolution>();
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

/// Passes that holds DSNode and Pool Handle information
struct DSNodePass : public ModulePass {
	public :
    static char ID;
    DSNodePass () : ModulePass ((intptr_t) &ID) { }
	virtual ~DSNodePass() {};
    const char *getPassName() const { return "DS Node And Pool Handle Pass"; }
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
#ifndef LLVA_KERNEL      
      AU.addPreserved<PoolAllocateGroup>();
      AU.addRequired<PoolAllocateGroup>();
#else 
      AU.addRequired<TDDataStructures>();
#endif
    };
  public:
  // FIXME: Provide better interfaces
#ifndef  LLVA_KERNEL
  PoolAllocateGroup * paPass;
#else
  TDDataStructures * TDPass;
#endif
  DSGraph & getDSGraph (Function & F);
  DSNode* getDSNode(const Value *V, Function *F);
  unsigned getDSNodeOffset(const Value *V, Function *F);
#ifndef LLVA_KERNEL  
  Value * getPoolHandle(const Value *V, Function *F, PA::FuncInfo &FI, bool collapsed = true);

  // Set of checked DSNodes
  std::set<DSNode *> CheckedDSNodes;

  // The set of values that already have run-time checks
  std::set<Value *> CheckedValues;
#endif  
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
    AU.addRequired<DSNodePass>();
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
}
