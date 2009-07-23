//===- DummyUse.cpp - Dummy Pass for SAFECode --------------------------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a dummy pass.  It does nothing except keep the pool
// allocation "analysis" results alive for subsequent passes.
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "dummy-use"

#include "safecode/DummyUse.h"

NAMESPACE_SC_BEGIN

char DummyUse::ID = 0;


static RegisterPass<DummyUse>
X("dummy-use", "Dummy pass to keep PA info live", true, true);

NAMESPACE_SC_END
