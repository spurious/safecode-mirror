#ifndef INSERT_BOUNDS_H
#define INSERT_BOUNDS_H

#include "llvm/Pass.h"
#include "ConvertUnsafeAllocas.h"
#include "/home/vadve/dhurjati/llvm/projects/poolalloc/include/poolalloc/PoolAllocate.h"
namespace llvm {

Pass *creatInsertPoolChecks();
using namespace CUA;

struct InsertPoolChecks : public Pass {
    public :
    const char *getPassName() const { return "Inserting pool checks for array bounds "; }
    virtual bool run(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<ConvertUnsafeAllocas>();
      AU.addRequired<PoolAllocate>();
      AU.addRequired<BUDataStructures>();
      AU.addRequired<TDDataStructures>();
    };
    private :
      CUA::ConvertUnsafeAllocas * cuaPass;
      PoolAllocate * paPass;
  Function *PoolCheck;
  void addPoolCheckProto(Module &M);
  void addPoolChecks(Module &M);
  Value * getPoolHandle(const Value *V, Function *F, PA::FuncInfo &FI);

};
}
#endif
