/**
 * This pass lowers all intrinsics used by SAFECode to appropriate runtime functions
 *
 *
 **/

#ifndef _LOWER_SAFECODE_INTRINSICS_H_
#define _LOWER_SAFECODE_INTRINSICS_H_

#include "llvm/Pass.h"
#include "llvm/Instructions.h"

#include "safecode/Config/config.h"

#include <vector>

namespace llvm {

  struct LowerSafecodeIntrinsic : public ModulePass {
  public:

    typedef struct IntrinsicMappingEntry {
      const char * intrinsicName;
      const char * functionName;
    } IntrinsicMappingEntry;

    static char ID;

 template<class Iterator>
   LowerSafecodeIntrinsic(Iterator begin, Iterator end) : ModulePass((intptr_t) &ID) {
    for(Iterator it = begin; it != end; ++it) {
      mReplaceList.push_back(*it);
    }
  }
 
 LowerSafecodeIntrinsic() : ModulePass((intptr_t) &ID), mReplaceList() {};

    virtual ~LowerSafecodeIntrinsic() {};
    virtual bool runOnModule(Module & M);
    virtual const char * getPassName() const { return "Lower intrinsic used by SAFECode to appropriate runtime implementation"; }
    virtual void getAnalysisUsage(AnalysisUsage &AU) const {
      AU.setPreservesCFG();
    };

  private:
    std::vector<IntrinsicMappingEntry> mReplaceList;
  };


}

#endif
