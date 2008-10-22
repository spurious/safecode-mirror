/// The replace function pass replaces all calls to a particular function
/// to another

#ifndef _REPLACE_FUCNTION_PASS_H_
#define _REPLACE_FUCNTION_PASS_H_

#include "llvm/Pass.h"
#include "llvm/Instructions.h"

#include "poolalloc/PoolAllocate.h"

#include "InsertPoolChecks.h"
#include "safecode/Config/config.h"

#include <string>

namespace llvm {

  struct ReplaceFunctionPass : public ModulePass {
  public:
    class ReplaceFunctionEntry {
    public:
      const char * orignalFunctionName;
      const char * newFunctionName;
      explicit ReplaceFunctionEntry(const char * origF, const char * newF) :
      orignalFunctionName(origF), newFunctionName(newF) {}
    };
    static char ID;
    static std::vector<ReplaceFunctionEntry> sReplaceList;

  ReplaceFunctionPass(const std::vector<ReplaceFunctionEntry> & replaceList) : 
    ModulePass((intptr_t) &ID),
      mReplaceList(replaceList) {};
  ReplaceFunctionPass() :
    ModulePass((intptr_t) &ID),
      mReplaceList(sReplaceList) {};
    virtual ~ReplaceFunctionPass() {};
    virtual bool runOnModule(Module & M);
    virtual const char * getPassName() const { return "Replace all uses of a function to another";}
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.addPreserved<EQTDDataStructures>();
      AU.addPreserved<PoolAllocateGroup>();
      AU.addPreserved<DSNodePass>();
      AU.setPreservesCFG();
    };

  private:
    const std::vector<ReplaceFunctionEntry> & mReplaceList;
  };


}

#endif
