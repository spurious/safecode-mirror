//===- PoolHandles.h - Find DSNodes and Pool Handles for SAFECode Passes -----//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a pass that interfaces with the DSA and Pool Allocation
// passes to lookup both DSNode and Pool Handle information.  This
// functionality was moved to another pass as what it needs to do is different
// for different configurations of SAFECode.
//
//===----------------------------------------------------------------------===//

#ifndef POOLHANDLES_H
#define POOLHANDLES_H

#include "llvm/Instructions.h"
#include "llvm/Pass.h"
#include "safecode/Config/config.h"

#ifndef LLVA_KERNEL
#include "poolalloc/PoolAllocate.h"
#endif

#include "dsa/DSNode.h"

namespace llvm {

/// Passes that holds DSNode and Pool Handle information
struct DSNodePass : public ModulePass {
	public :
    static char ID;
    DSNodePass () : ModulePass ((intptr_t) &ID) { }
    virtual ~DSNodePass() {};
    const char *getPassName() const {
      return "DS Node And Pool Allocation Handle Pass";
    }
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
#ifndef LLVA_KERNEL      
      AU.addRequiredTransitive<PoolAllocateGroup>();
#else 
      AU.addRequired<TDDataStructures>();
#endif
      AU.setPreservesAll();
    };

  public:
    // FIXME: Provide better interfaces
#ifndef  LLVA_KERNEL
    PoolAllocateGroup * paPass;
#else
    TDDataStructures * TDPass;
#endif
    DSGraph * getDSGraph (Function & F);
    DSNode* getDSNode(const Value *V, Function *F);
    unsigned getDSNodeOffset(const Value *V, Function *F);
#ifndef LLVA_KERNEL  
    Value * getPoolHandle(const Value *V, Function *F, PA::FuncInfo &FI, bool collapsed = true);
#endif

    void addCheckedDSNode(const DSNode * node);
    void addCheckedValue(const Value * value);
    bool isDSNodeChecked(const DSNode * node) const;
    bool isValueChecked(const Value * val) const;
  private:
    // Set of checked DSNodes
    std::set<const DSNode *> CheckedDSNodes;

    // The set of values that already have run-time checks
    std::set<const Value *> CheckedValues;
};
}
#endif
