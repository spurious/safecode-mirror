#ifndef CONVERT_ALLOCA_H
#define CONVERT_ALLOCA_H

#include "llvm/Pass.h"
#include "ArrayBoundsCheck.h"
#include "StackSafety.h"
namespace llvm {


Pass *createConvertUnsafeAllocas();

using namespace ABC;
using namespace CSS;

 struct MallocPass : public FunctionPass
 {
   private:
inline bool changeType (Instruction * Inst);
   
   public:
virtual bool runOnFunction (Function &F);
 };

 
namespace CUA {
struct ConvertUnsafeAllocas : public Pass {
    public :
    const char *getPassName() const { return "Array Bounds Check"; }
    virtual bool run(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<ArrayBoundsCheck>();
      AU.addRequired<checkStackSafety>();
      AU.addRequired<BUDataStructures>();
      AU.addRequired<TDDataStructures>();
      AU.setPreservesAll();
    }

  DSNode * getDSNode(const Value *I, Function *F);
  DSNode * getTDDSNode(const Value *I, Function *F);

  std::vector<Instruction *>  & getUnsafeGetElementPtrsFromABC() {
    assert(abcPass != 0 && "First run the array bounds pass correctly");
    return abcPass->UnsafeGetElemPtrs;
  }  

    private :
      TDDataStructures * tddsPass;
      BUDataStructures * budsPass;
      ArrayBoundsCheck * abcPass;
      checkStackSafety * cssPass;
  
    std::vector<DSNode *> unsafeAllocaNodes;
    std::set<DSNode *> reachableAllocaNodes; 
    bool markReachableAllocas(DSNode *DSN);
    bool markReachableAllocasInt(DSNode *DSN);
    void TransformAllocasToMallocs(std::vector<DSNode *> & unsafeAllocaNodes);
    void getUnsafeAllocsFromABC();
};
}
} 
#endif
