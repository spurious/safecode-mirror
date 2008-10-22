/// Replace function pass replaces all uses of a function to another


#include <iostream>
#include <set>
#include <map>
#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"
#include "safecode/Config/config.h"
#include "safecode/VectorListHelper.h"
#include "safecode/ReplaceFunctionPass.h"

using namespace llvm;

std::vector<ReplaceFunctionPass::ReplaceFunctionEntry> ReplaceFunctionPass::sReplaceList;

char ReplaceFunctionPass::ID = 0;

static RegisterPass<ReplaceFunctionPass> passReplaceFunction 
("replace-function-use", "Replace all uses of a function to another");

namespace llvm {

  ////////////////////////////////////////////////////////////////////////////
  // ReplaceFunctionPass Methods
  ////////////////////////////////////////////////////////////////////////////

  bool
  ReplaceFunctionPass::runOnModule(Module & M) {
    for (std::vector<ReplaceFunctionEntry>::const_iterator it = mReplaceList.begin(), end = mReplaceList.end(); it != end; ++it) {
      Function * origF = M.getFunction(it->orignalFunctionName);
      if (origF) {
        Constant * newF = M.getOrInsertFunction(it->newFunctionName, origF->getFunctionType());
        origF->replaceAllUsesWith(newF);
      }
    }   
    return true;
  }
}
