/// This header file defines the analysis and transformation parts of
/// code duplication stuffs.

#ifndef _CODE_DUPLICATION_H_
#define _CODE_DUPLICATION_H_

#include <map>
#include "llvm/Module.h"
#include "llvm/Pass.h"
#include "llvm/Instructions.h"
#include "llvm/ADT/SmallVector.h"

namespace llvm {

  /// This module analyzes the side effects of codes to see:
  ///
  /// 1. Whether we can duplicate the codes.
  /// 2. What parameters are needed to duplicate the codes.
  ///
  struct CodeDuplicationAnalysis : public ModulePass {
  public:
    static char ID;
  CodeDuplicationAnalysis() : ModulePass((intptr_t) &ID) {};
    void getAnalysisUsage(AnalysisUsage & AU) const {
      AU.setPreservesAll();
      AU.setPreservesCFG();
    };
    virtual bool doInitialization(Module & M);
    virtual bool doFinalization(Module & M);
    virtual const char * getPassName() const { return "Code Duplication Analysis"; };
    virtual ~CodeDuplicationAnalysis() {};
    virtual bool runOnModule(Module & m);
    /// Arguments required to turn a basic block to "pure" basic block
    typedef SmallVector<Instruction *, 8> InputArgumentsTy;
    /// FIXME: Make an iterator interfaces
    typedef std::map<BasicBlock*, InputArgumentsTy> BlockInfoTy;
    const BlockInfoTy & getBlockInfo() const { return mBlockInfo; }
  private:
    BlockInfoTy mBlockInfo;
    void calculateBBArgument(BasicBlock * BB, InputArgumentsTy & args);
  };

  /// Remove all self-loop edges from every basic blocks
  struct RemoveSelfLoopEdge : public FunctionPass {
  public:
    static char ID;
  RemoveSelfLoopEdge() : FunctionPass((intptr_t) & ID) {};
    const char * getPassName() const { return "Remove all self-loop edges from every basic block"; };
    virtual bool runOnFunction(Function & F);
    virtual ~RemoveSelfLoopEdge() {};
  };

  struct DuplicateCodeTransform : public ModulePass {
  public:
  DuplicateCodeTransform(): ModulePass((intptr_t) & ID) {};
    static char ID;
    virtual ~DuplicateCodeTransform() {};
    const char * getPassName() const { return "Duplicate codes for SAFECode checking"; };
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addRequired<CodeDuplicationAnalysis>();
    }
    virtual bool runOnModule(Module &M);

  private:
    void wrapCheckingRegionAsFunction(Module & M, const BasicBlock * bb, 
				      const CodeDuplicationAnalysis::InputArgumentsTy & args);
  };
}

#endif
