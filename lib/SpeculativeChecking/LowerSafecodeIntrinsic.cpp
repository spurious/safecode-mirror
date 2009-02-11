//===- LowerSafecodeIntrinsic.cpp: ----------------------------------------===//
//
//                          The SAFECode Compiler
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This pass lowers all intrinsics added by SAFECode to appropriate calls to
// run-time functions in the run-time implementation.
//
//===----------------------------------------------------------------------===//

#include "llvm/Module.h"
#include "safecode/LowerSafecodeIntrinsic.h"

using namespace llvm;

char LowerSafecodeIntrinsic::ID = 0;

static RegisterPass<LowerSafecodeIntrinsic> passReplaceFunction 
("lower-sc-intrinsic", "Replace all uses of a function to another");

namespace llvm {

  ////////////////////////////////////////////////////////////////////////////
  // LowerSafecodeIntrinsic Methods
  ////////////////////////////////////////////////////////////////////////////

  bool
  LowerSafecodeIntrinsic::runOnModule(Module & M) {
    for (std::vector<IntrinsicMappingEntry>::const_iterator it = mReplaceList.begin(), end = mReplaceList.end(); it != end; ++it) {
      if (it->intrinsicName == it->functionName)
        continue;

      Function * origF = M.getFunction(it->intrinsicName);
      if (origF) {
        Constant * newF = M.getOrInsertFunction(it->functionName, origF->getFunctionType());
        origF->replaceAllUsesWith(newF);
      }
    }   
    return true;
  }
}
