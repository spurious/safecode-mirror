//===- llvm/Transforms/IPO/CZero/CZero.h  - CZero passes  -*- C++ -*---------=//
//
// This file defines a set of utilities for CZero checks on pointers and
// dynamic memory
// 
//===----------------------------------------------------------------------===//

#ifndef LLVM_CZERO_H
#define LLVM_CZERO_H

using namespace llvm;

#include "llvm/Pass.h"


FunctionPass* createCZeroUninitPtrPass();
// FunctionPass* createCZeroLivePtrs();

#endif
