//===- StackSafety.h                                      -*- C++ -*---------=//
//
// This file defines checks for stack safety
// 
//===----------------------------------------------------------------------===//

#ifndef LLVM_STACKSAFETY_H
#define LLVM_STACKSAFETY_H

#include "llvm/Pass.h"

Pass* createStackSafetyPass();

#endif
