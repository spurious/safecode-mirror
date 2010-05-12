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

#include "llvm/LLVMContext.h"
#include "llvm/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Support/InstVisitor.h"

#include "safecode/Config/config.h"
#include "safecode/SAFECodeConfig.h"

#include "dsa/DSGraph.h"
#include "poolalloc/PoolAllocate.h"

using namespace llvm;

NAMESPACE_SC_BEGIN

/// Passes that holds DSNode and Pool Handle information
struct DSNodePass : public ModulePass {
	public :
    static char ID;
    DSNodePass () : ModulePass ((intptr_t) &ID) { }
    const char *getPassName() const {
      return "DS Node And Pool Allocation Handle Pass";
    }
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      getAnalysisUsageForPoolAllocation(AU);
      AU.setPreservesAll();
    };

    // Get analysis usage for DSA. Make sure all SAFECode passes have the same
    // dependency on DSA.
    static void getAnalysisUsageForDSA(AnalysisUsage &AU);

    // Get analysis usage for Pool Allocation. It is a hack to lie to the
    // PassManager to make sure Pool Allocation only run only one time.
    // This function tells the PassManager it preseves DSA and PoolAllocation
    // results, which is clearly a lie.
    static void getAnalysisUsageForPoolAllocation(AnalysisUsage &AU);

    static void preservePAandDSA(AnalysisUsage &AU);
    
    //
    // Method: releaseMemory()
    //
    // Description:
    //  This method is called by the pass manager when the analysis results of
    //  this pass are either invalidated or no longer needed.  This method
    //  should free up memory and prepare the analysis pass for its next
    //  execution by the pass manager.
    //
    void releaseMemory () {
      //
      // Clear out the set of checked values and checked DSNodes.
      //
      CheckedValues.clear();
      CheckedDSNodes.clear();
    }

    // FIXME: Provide better interfaces
    PoolAllocateGroup * paPass;
    DSGraph * getDSGraph (Function & F);
    DSNode* getDSNode(const Value *V, Function *F);
    //
    // Try to find the DSNode for the global variable
    //
    DSNode * getDSNodeForGlobalVariable(const GlobalValue * GV);
    unsigned getDSNodeOffset(const Value *V, Function *F);
    Value * getPoolHandle(const Value *V, Function *F, PA::FuncInfo &FI, bool collapsed = true);
    const Type * getPoolType (void) {
      return (paPass->getPoolType(&(getGlobalContext())));
    }

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

//
// Pass: PoolMDPass
//
// Description:
//  This pass takes pool handle information and instruments the LLVM bitcode
//  with meta-data linking each pointer used in a GEP or load/store with the
//  pool to which it belongs.  Additionally, it records DSA information about
//  each pool in the metadata, freeing most passes from having to consult the
//  DSA passes.
//
struct PoolMDPass : public ModulePass,
                    public InstVisitor<PoolMDPass> {
	public :
    static char ID;
    PoolMDPass () : ModulePass ((intptr_t) &ID) { }
    const char *getPassName() const {
      return "Pool Handle Metadata Creation Pass";
    }
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      DSNodePass::preservePAandDSA(AU);
      AU.addRequired<DSNodePass>();

      AU.addPreserved<DSNodePass>();
      AU.setPreservesCFG();
    };

    // Instruction visitor methods
    void visitAllocaInst(AllocaInst & AI);
    void visitLoadInst(LoadInst & I);
    void visitStoreInst(StoreInst & I);
    void visitCallInst(CallInst & CI);
    void visitGetElementPtrInst(GetElementPtrInst & GEP);
    void visitICmpInst (ICmpInst & CI);
    void visitPtrToIntInst(PtrToIntInst &I);

  private:
    // Pointers to analysis passes that we use
    DSNodePass * dsnPass;

    // Data structure containing value to pool metadata nodes
    std::vector<MDNode *> ValueToPoolNodes;

    // Data structure containing alignment metadata nodes
    std::vector<MDNode *> AlignmentNodes;

    // Private methods
    void createPoolMetaData (GlobalVariable * GV);
    void createPoolMetaData (Value * P, Function * F);
    void createGlobalMetaData (Module & M);
    void createByValMetaData (Module & M);
    void createOffsetMetaData (LoadInst & LI);
};

//
// Pass: QueryPoolPass
//
// Description:
//  This pass is an analysis pass that reads the metadata added by the
//  PoolMDPass.  This pass makes querying the pool handle and DSA information
//  easier for other passes and centralizes the reading of the metadata
//  information.
//
struct QueryPoolPass : public ModulePass {
  public:
    // Pass ID variable
    static char ID;

    // Pass Manager methods
    QueryPoolPass () : ModulePass ((intptr_t) &ID) {}
    virtual bool runOnModule (Module & M);
    const char *getPassName() const {
      return "Pool Handle Metadata Query Pass";
    }
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesAll();
    };

    // Data query methods
    Value * getPool (const Value * V);
    const Type * getPoolType (void);
    unsigned getDSFlags (const Value * V) {
      return FlagMap[V];
    }

    bool isTypeKnown (const Value * V) {
      return FoldedMap[V];
    }

    Value * getAlignment (const LoadInst * LI) {
      return AlignMap[LI];
    }

  protected:
    std::map<const Value *, Value *>    PoolMap;
    std::map<const Value *, bool>       FoldedMap;
    std::map<const Value *, unsigned>   FlagMap;
    std::map<const LoadInst *, Value *> AlignMap;
};

//
// Pass: RemovePoolMDPass
//
// Description:
//  This pass removes meta-data inserted by the PoolMDPass.  This is required
//  for LLVM 2.6 because the meta-data that we add is usable during analysis
//  and transformation but is invalid input to the BitcodeWriter.
//
struct RemovePoolMDPass : public ModulePass,
                          public InstVisitor<PoolMDPass> {
	public :
    static char ID;
    RemovePoolMDPass () : ModulePass ((intptr_t) &ID) { }
    const char *getPassName() const {
      return "Pool Handle Metadata Removal Pass";
    }
    virtual bool runOnModule(Module &M);
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesCFG();
    };

  private:
};

NAMESPACE_SC_END
#endif
