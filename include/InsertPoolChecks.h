#ifndef INSERT_BOUNDS_H
#define INSERT_BOUNDS_H

#include "llvm/Pass.h"
#include "ConvertUnsafeAllocas.h"
#include "SafeDynMemAlloc.h"
#include "/home/vadve/kowshik/llvm/projects/poolalloc/include/poolalloc/PoolAllocate.h"

namespace llvm {

Pass *creatInsertPoolChecks();
using namespace CUA;

struct InsertPoolChecks : public Pass {
    public :
    const char *getPassName() const { return "Inserting pool checks for array bounds "; }
    virtual bool run(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<CompleteBUDataStructures>();
      AU.addRequired<TDDataStructures>();
      AU.addRequired<ConvertUnsafeAllocas>();
      AU.addRequired<PoolAllocate>();
      AU.addRequired<EmbeCFreeRemoval>();
    };
    private :
      CUA::ConvertUnsafeAllocas * cuaPass;
      PoolAllocate * paPass;
  EmbeCFreeRemoval *efPass;
  CompleteBUDataStructures *budsPass;
  Function *PoolCheck;
  void addPoolCheckProto(Module &M);
  void addPoolChecks(Module &M);
  Value * getPoolHandle(const Value *V, Function *F, PA::FuncInfo &FI);

};
}
#endif
