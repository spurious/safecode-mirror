//===- StackSafety.h                                      -*- C++ -*---------=//
//
// This file defines checks for stack safety
// 
//===----------------------------------------------------------------------===//

#ifndef LLVM_STACKSAFETY_H
#define LLVM_STACKSAFETY_H

#include "llvm/Pass.h"
#include "llvm/Analysis/DataStructure.h"
#include "llvm/Analysis/DSGraph.h"
#include "llvm/Analysis/DSNode.h"
#include <set>
namespace llvm {

Pass* createStackSafetyPass();

namespace CSS {

struct checkStackSafety : public Pass {
    
  public :
    std::vector<DSNode *> AllocaNodes;
    const char *getPassName() const { return "Stack Safety Check";}
    virtual bool run(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
     AU.setPreservesAll();      
     AU.addRequired<TDDataStructures>();
    }
  private :
    std::set<DSNode *> reachableAllocaNodes; 
    bool markReachableAllocas(DSNode *DSN);
    bool markReachableAllocasInt(DSNode *DSN);
  };
}
}
#endif
