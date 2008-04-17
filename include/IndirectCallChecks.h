#ifndef _INDIRECT_CALL_CHECKS_H_
#define _INDIRECT_CALL_CHECKS_H_

#include "safecode/Config/config.h"
#include "llvm/Pass.h"

namespace llvm {
    ModulePass *createIndirectCallChecksPass();
}

#endif
