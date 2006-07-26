//===- llvm/Transforms/IPO/EmbeC/EmbeC.h  - CZero passes  -*- C++ -*---------=//
//
// This file defines a set of utilities for EmbeC checks on pointers and
// dynamic memory
// 
//===----------------------------------------------------------------------===//

#ifndef LLVM_EMBEC_H
#define LLVM_EMBEC_H


#include "safecode/Config/config.h"
#include "llvm/Pass.h"
#include "poolalloc/PoolAllocate.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/DerivedTypes.h"
#include "llvm/Constants.h"
#include "llvm/Support/CFG.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/Analysis/DataStructure/DataStructure.h"
#include "llvm/Analysis/DataStructure/DSGraph.h"
#include "llvm/Support/Debug.h"
#include <set>
#include <map>
#include <string>
using std::set;
using std::map;

using namespace llvm;
#ifndef LLVA_KERNEL
using namespace PA;
#endif
Pass* createEmbeCFreeRemovalPass();


namespace llvm {

  struct EmbeCFreeRemoval : public ModulePass {
    
    // The function representing 'poolmakeunfreeable'
    Function *PoolMakeUnfreeable;

    Function *PoolCheck;

    bool runOnModule(Module &M);
    std::vector<Value *> Visited;
    static const std::string PoolI;
    static const std::string PoolA;
    static const std::string PoolAA;
    static const std::string PoolF;
    static const std::string PoolD;
    static const std::string PoolMUF;
    static const std::string PoolCh;
    
    void checkPoolSSAVarUses(Function *F, Value *V, 
			     map<Value *, set<Instruction *> > &FuncAllocs, 
			     map<Value *, set<Instruction *> > &FuncFrees, 
			     map<Value *, set<Instruction *> > &FuncDestroy);

    void propagateCollapsedInfo(Function *F, Value *V);
    DSNode *guessDSNode(Value *v, DSGraph &G, PA::FuncInfo *PAFI);
    void guessPoolPtrAndInsertCheck(PA::FuncInfo *PAFI, Value *oldI, Instruction  *I, Value *pOpI, DSGraph &oldG);
      
    void insertNonCollapsedChecks(Function *Forig, Function *F, DSNode *DSN);

    void addRuntimeChecks(Function *F, Function *Forig);
    
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
#ifndef LLVA_KERNEL
      AU.addRequired<EquivClassGraphs>();
      AU.addRequired<PoolAllocate>();
#endif      
      AU.addRequired<CompleteBUDataStructures>();
      AU.addRequired<TDDataStructures>();
      AU.addRequired<CallGraph>();
      AU.setPreservesAll();
    }

    // Maps from a function to a set of Pool pointers and DSNodes from the 
    // original function corresponding to collapsed pools.
    map <Function *, set<Value *> > CollapsedPoolPtrs;

    
  private:
    
    Module *CurModule;

    TDDataStructures *TDDS;
    EquivClassGraphs *BUDS;
#ifndef LLVA_KERNEL    
    PoolAllocate *PoolInfo;
#endif    
    bool moduleChanged;
    bool hasError;
    
    // The following maps are only for pool pointers that escape a function.
    // Associates function with set of pools that are freed or alloc'ed using 
    // pool_free or pool_alloc but not destroyed within the function.
    // These have to be pool pointer arguments to the function
    map<Function *, set<Value *> > FuncFreedPools;
    map<Function *, set<Value *> > FuncAllocedPools;
    map<Function *, set<Value *> > FuncDestroyedPools;

  };  
  const std::string EmbeCFreeRemoval::PoolI = "poolinit";
  const std::string EmbeCFreeRemoval::PoolA = "poolalloc";
  const std::string EmbeCFreeRemoval::PoolF = "poolfree";
  const std::string EmbeCFreeRemoval::PoolD = "pooldestroy";
  const std::string EmbeCFreeRemoval::PoolMUF = "poolmakeunfreeable";
  const std::string EmbeCFreeRemoval::PoolCh = "poolcheck";
  const std::string EmbeCFreeRemoval::PoolAA = "poolregister";
}

#endif
