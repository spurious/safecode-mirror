#ifndef INSERT_BOUNDS_H
#define INSERT_BOUNDS_H

#include "safecode/Config/config.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Pass.h"
#include "ArrayBoundsCheck.h"
#include "ConvertUnsafeAllocas.h"

#ifndef LLVA_KERNEL
#include "SafeDynMemAlloc.h"
#include "poolalloc/PoolAllocate.h"
#endif

namespace llvm {

ModulePass *creatInsertPoolChecks();
using namespace CUA;

struct InsertPoolChecks : public ModulePass {
    private :
    // Flags whether we want to do dangling checks
    bool DanglingChecks;

    public :
    static char ID;
    InsertPoolChecks (bool DPChecks = false)
      : ModulePass ((intptr_t) &ID) {
      DanglingChecks = DPChecks;
    }
    const char *getPassName() const { return "Inserting pool checks for array bounds "; }
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
//      AU.addRequired<CompleteBUDataStructures>();
//      AU.addRequired<TDDataStructures>();
#ifndef LLVA_KERNEL      
      AU.addRequired<EquivClassGraphs>();
      AU.addRequired<ArrayBoundsCheck>();
      AU.addRequired<EmbeCFreeRemoval>();
      AU.addRequired<TargetData>();
      AU.addRequired<DominatorTree>();
      AU.addPreserved<PoolAllocateGroup>();
#else 
      AU.addRequired<TDDataStructures>();
#endif
    };
    private :
      ArrayBoundsCheck * abcPass;
#ifndef  LLVA_KERNEL
  PoolAllocateGroup * paPass;
  EmbeCFreeRemoval *efPass;
  TargetData * TD;
#else
  TDDataStructures * TDPass;
#endif  
  Constant *RuntimeInit;
  Constant *PoolCheck;
  Constant *PoolCheckUI;
  Constant *PoolCheckArray;
  Constant *PoolCheckArrayUI;
  Constant *ExactCheck;
  Constant *ExactCheck2;
  Constant *FunctionCheck;
  Constant *GetActualValue;
  Constant *StackFree;
  void addPoolCheckProto(Module &M);
  void addPoolChecks(Module &M);
  void addGetElementPtrChecks(BasicBlock * BB);
  void addGetActualValue(llvm::ICmpInst*, unsigned int);
  bool insertExactCheck (GetElementPtrInst * GEP);
  bool insertExactCheck (Instruction * , Value *, Value *, Instruction *);
  DSNode* getDSNode(const Value *V, Function *F);
  unsigned getDSNodeOffset(const Value *V, Function *F);
  void addLoadStoreChecks(Module &M);
  void registerStackObjects (Module &M);
  void registerAllocaInst(AllocaInst *AI, AllocaInst *AIOrig);
  void addExactCheck (Value * P, Value * I, Value * B, Instruction * InsertPt);
  void addExactCheck2 (Value * B, Value * R, Value * C, Instruction * InsertPt);
  DSGraph & getDSGraph (Function & F);
#ifndef LLVA_KERNEL  
  void addLSChecks(Value *Vnew, const Value *V, Instruction *I, Function *F);
  Value * getPoolHandle(const Value *V, Function *F, PA::FuncInfo &FI, bool collapsed = true);
  void registerGlobalArraysWithGlobalPools(Module &M);
#else
  void addLSChecks(Value *V, Instruction *I, Function *F);
  Value * getPoolHandle(const Value *V, Function *F);
#endif  
};
}
#endif
