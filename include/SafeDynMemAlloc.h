//===- llvm/Transforms/IPO/EmbeC/EmbeC.h  - CZero passes  -*- C++ -*---------=//
//
// This file defines a set of utilities for EmbeC checks on pointers and
// dynamic memory
// 
//===----------------------------------------------------------------------===//

#ifndef LLVM_EMBEC_H
#define LLVM_EMBEC_H

#include "llvm/Pass.h"
using namespace llvm;
Pass* createEmbeCFreeRemovalPass();

#endif
