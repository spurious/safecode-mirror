//===- convert.h - Promote unsafe alloca instructions to heap allocations ----//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a pass that promotes unsafe stack allocations to heap
// allocations.  It also updates the pointer analysis results accordingly.
//
// This pass relies upon the abcpre, abc, and checkstack safety passes.
//
//===----------------------------------------------------------------------===//

#ifndef CONVERT_ALLOCA_H
#define CONVERT_ALLOCA_H

#include "dsa/DataStructure.h"
#include "llvm/Pass.h"
#include "ArrayBoundsCheck.h"
#include "StackSafety.h"
#include "llvm/Analysis/Dominators.h"
#include "llvm/Target/TargetData.h"
#include "safecode/Config/config.h"

#ifndef LLVA_KERNEL
#include "poolalloc/PoolAllocate.h"
#endif

#include <set>

namespace llvm {

ModulePass *createConvertUnsafeAllocas();

using namespace ABC;
using namespace CSS;

struct MallocPass : public FunctionPass {
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
  public:
    static char ID;
    ConvertUnsafeAllocas (intptr_t IDp = (intptr_t) (&ID)) : ModulePass (IDp) {}
    const char *getPassName() const { return "Convert Unsafe Allocas"; }
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<ArrayBoundsCheck>();
      AU.addRequired<checkStackSafety>();
      AU.addRequired<CompleteBUDataStructures>();
      AU.addRequired<TDDataStructures>();
      AU.addRequired<TargetData>();
      AU.addRequired<DominatorTree>();
      AU.addRequired<DominanceFrontier>();

      AU.addPreserved<ArrayBoundsCheck>();

      // Does not preserve the BU or TD graphs
#ifdef LLVA_KERNEL       
      AU.setPreservesAll();
#endif            
    }

    DSNode * getDSNode(const Value *I, Function *F);
    DSNode * getTDDSNode(const Value *I, Function *F);

    std::map<BasicBlock*,std::set<Instruction *>*> &
    getUnsafeGetElementPtrsFromABC() {
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

  protected:
    TDDataStructures * tddsPass;
    BUDataStructures * budsPass;
    ArrayBoundsCheck * abcPass;
    checkStackSafety * cssPass;

    TargetData *TD;

#ifdef LLVA_KERNEL
    Constant *kmalloc;
    Constant *StackPromote;
#endif

    std::list<DSNode *> unsafeAllocaNodes;
    std::set<DSNode *> reachableAllocaNodes; 
    bool markReachableAllocas(DSNode *DSN);
    bool markReachableAllocasInt(DSNode *DSN);
    void TransformAllocasToMallocs(std::list<DSNode *> & unsafeAllocaNodes);
    void TransformCSSAllocasToMallocs(std::vector<DSNode *> & cssAllocaNodes);
    void getUnsafeAllocsFromABC();
    void TransformCollapsedAllocas(Module &M);
    virtual void InsertFreesAtEnd(MallocInst *MI);
    virtual Value * promoteAlloca(AllocaInst * AI, DSNode * Node);
};

//
// Struct: PAConvertUnsafeAllocas 
//
// Description:
//  This is an LLVM transform pass that is similar to the original
//  ConvertUnsafeAllocas pass.  However, instead of promoting unsafe stack
//  allocations to malloc instructions, it will promote them to use special
//  allocation functions within the pool allocator run-time.
//
// Notes:
//  o) By using the pool allocator run-time, this pass should generate faster
//     code than the original ConvertUnsafeAllocas pass.
//  o) This pass requires that a Pool Allocation pass be executed before this
//     transform is executed.
//
struct PAConvertUnsafeAllocas : public ConvertUnsafeAllocas {
  private:
    PoolAllocateGroup * paPass;

  protected:
    virtual void InsertFreesAtEndNew(Value * PH, Instruction  *MI);
    virtual Value * promoteAlloca(AllocaInst * AI, DSNode * Node);

  public:
    static char ID;
    PAConvertUnsafeAllocas () : ConvertUnsafeAllocas ((intptr_t)(&ID)) {}
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<ArrayBoundsCheck>();
      AU.addRequired<checkStackSafety>();
      AU.addRequired<CompleteBUDataStructures>();
      AU.addRequired<TDDataStructures>();
      AU.addRequired<TargetData>();
      AU.addRequired<DominatorTree>();
      AU.addRequired<DominanceFrontier>();

      AU.addPreserved<ArrayBoundsCheck>();
      AU.addPreserved<PoolAllocateGroup>();

      // Does not preserve the BU or TD graphs
#ifdef LLVA_KERNEL       
      AU.setPreservesAll();
#endif            
    }
};

}
} 
#endif
