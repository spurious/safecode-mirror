#ifndef INSERT_BOUNDS_H
#define INSERT_BOUNDS_H

#include "llvm/Pass.h"
#include "ConvertUnsafeAllocas.h"
#include "SafeDynMemAlloc.h"
#include "/home/vadve/dhurjati/llvm/projects/poolalloc.safecode/lib/PoolAllocate/PoolAllocate.h"

namespace llvm {

ModulePass *creatInsertPoolChecks();
using namespace CUA;

struct InsertPoolChecks : public ModulePass {
    public :
    const char *getPassName() const { return "Inserting pool checks for array bounds "; }
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<ConvertUnsafeAllocas>();
//      AU.addRequired<CompleteBUDataStructures>();
//      AU.addRequired<TDDataStructures>();
      AU.addRequired<EquivClassGraphs>();
      AU.addRequired<PoolAllocate>();
      AU.addRequired<EmbeCFreeRemoval>();
    };
    private :
      CUA::ConvertUnsafeAllocas * cuaPass;
      PoolAllocate * paPass;
  EmbeCFreeRemoval *efPass;
  EquivClassGraphs *equivPass;
  Function *PoolCheck;
  Function *ExactCheck;
  void addPoolCheckProto(Module &M);
  void addPoolChecks(Module &M);
  Value * getPoolHandle(const Value *V, Function *F, PA::FuncInfo &FI);
  DSNode* getDSNode(const Value *V, Function *F);
  unsigned getDSNodeOffset(const Value *V, Function *F);

};
}
#endif
