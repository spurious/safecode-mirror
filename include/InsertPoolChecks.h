#ifndef INSERT_BOUNDS_H
#define INSERT_BOUNDS_H

#include "safecode/Config/config.h"
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

  struct InsertPoolChecks : public FunctionPass {
    private :
      // Flags whether we want to do dangling checks
      bool DanglingChecks;

    public :
      static char ID;
      InsertPoolChecks (bool DPChecks = false)
        : FunctionPass ((intptr_t) &ID) {
          DanglingChecks = DPChecks;
        }
      const char *getPassName() const { return "Inserting Pool checks Pass"; }
      virtual bool doInitialization(Module &M);
      virtual bool runOnFunction(Function &F);
      virtual void getAnalysisUsage(AnalysisUsage &AU) const {
        //      AU.addRequired<CompleteBUDataStructures>();
        //      AU.addRequired<TDDataStructures>();
#ifndef LLVA_KERNEL      
        AU.addRequired<EquivClassGraphs>();
        AU.addRequired<ArrayBoundsCheck>();
        AU.addRequired<EmbeCFreeRemoval>();
        AU.addRequired<TargetData>();
//        AU.addPreserved<PoolAllocateGroup>();
#else 
        AU.addRequired<TDDataStructures>();
#endif
      };
    private :
      void addPoolChecks(Function &F);
      void addGetElementPtrChecks(BasicBlock * BB);
      void addGetActualValue(llvm::ICmpInst*, unsigned int);
      bool insertExactCheck (GetElementPtrInst * GEP);
      bool insertExactCheck (Instruction * , Value *, Value *, Instruction *);
      void addLoadStoreChecks(Function &F);
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
      ArrayBoundsCheck * abcPass;
#ifndef  LLVA_KERNEL
      PoolAllocateGroup * paPass;
      EmbeCFreeRemoval *efPass;
      TargetData * TD;
#else
      TDDataStructures * TDPass;
#endif  
      // Set of checked DSNodes
      std::set<DSNode *> CheckedDSNodes;

      // The set of values that already have run-time checks
      std::set<Value *> CheckedValues;

      void registerStackObjects (Function & F);
      void registerAllocaInst(AllocaInst *AI, AllocaInst *AIOrig);
      DSNode* getDSNode(const Value *V, Function *F);
      unsigned getDSNodeOffset(const Value *V, Function *F);
      
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
      void addCheckProto(Module &M);
  };
}
#endif
