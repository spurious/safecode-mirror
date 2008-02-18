#ifndef CONVERT_ALLOCA_H
#define CONVERT_ALLOCA_H

#include "dsa/DataStructure.h"
#include "llvm/Pass.h"
#include "ArrayBoundsCheck.h"
#include "StackSafety.h"
#include "llvm/Target/TargetData.h"
#include "safecode/Config/config.h"

#include <set>

namespace llvm {


ModulePass *createConvertUnsafeAllocas();

using namespace ABC;
using namespace CSS;
struct MallocPass : public FunctionPass
{
  private:
    // Private data
    Constant * memsetF;
    DominatorTree * domTree;

    // Private methods
    inline bool changeType (DSGraph & TDG, Instruction * Inst);
    inline bool TypeContainsPointer(const Type *Ty);

  public:
    static char ID;
    MallocPass() : FunctionPass((intptr_t)(&ID)) {}
    const char *getPassName() const { return "Malloc Pass"; }
    virtual bool runOnFunction (Function &F);
    virtual bool doInitialization (Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<TargetData>();
      AU.addRequired<TDDataStructures>();
#ifdef LLVA_KERNEL
      AU.setPreservesAll();
#else
      //AU.setPreservesAll();
#endif     
    }
};

namespace CUA {
struct ConvertUnsafeAllocas : public ModulePass {
    public :
    static char ID;
    ConvertUnsafeAllocas () : ModulePass ((intptr_t)(&ID)) {}
    const char *getPassName() const { return "Convert Unsafe Allocas"; }
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<ArrayBoundsCheck>();
      AU.addRequired<checkStackSafety>();
      AU.addRequired<CompleteBUDataStructures>();
      AU.addRequired<TDDataStructures>();
      AU.addRequired<TargetData>();

      AU.addPreserved<ArrayBoundsCheck>();
      // Does not preserve the BU or TD graphs
#ifdef LLVA_KERNEL       
      AU.setPreservesAll();
#else
      //AU.setPreservesAll();
#endif            
    }

  DSNode * getDSNode(const Value *I, Function *F);
  DSNode * getTDDSNode(const Value *I, Function *F);

  std::map<BasicBlock*,std::set<Instruction *>*> & getUnsafeGetElementPtrsFromABC() {
    assert(abcPass != 0 && "First run the array bounds pass correctly");
    return abcPass->UnsafeGetElemPtrs;
  }  

  std::set<Instruction *> * getUnsafeGetElementPtrsFromABC(BasicBlock * BB) {
    assert(abcPass != 0 && "First run the array bounds pass correctly");
    return abcPass->getUnsafeGEPs (BB);
  }  

  // The set of Malloc Instructions that are a result of conversion from
  // alloca's due to static array bounds detection failure
  std::set<const MallocInst *>  ArrayMallocs;

    private :
      TDDataStructures * tddsPass;
      BUDataStructures * budsPass;
      ArrayBoundsCheck * abcPass;
      checkStackSafety * cssPass;
  TargetData *TD;
  
#ifdef LLVA_KERNEL
Function *kmalloc;
Function *StackPromote;
#endif
    std::list<DSNode *> unsafeAllocaNodes;
    std::set<DSNode *> reachableAllocaNodes; 
    bool markReachableAllocas(DSNode *DSN);
    bool markReachableAllocasInt(DSNode *DSN);
    void TransformAllocasToMallocs(std::list<DSNode *> & unsafeAllocaNodes);
    void TransformCSSAllocasToMallocs(std::vector<DSNode *> & cssAllocaNodes);
    void getUnsafeAllocsFromABC();
    void TransformCollapsedAllocas(Module &M);
  void InsertFreesAtEnd(MallocInst *MI);

};
}

} 
#endif
