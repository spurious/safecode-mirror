//===- AllocatorInfo.cpp ----------------------------------------*- C++ -*----//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// Define the abstraction of a pair of allocator / deallocator, including:
//
//   * The size of the object being allocated. 
//   * Whether the size may be a constant, which can be used for exactcheck
// optimization.
//
//===----------------------------------------------------------------------===//

#include "safecode/Support/AllocatorInfo.h"

#include "llvm/Instructions.h"
#include "llvm/Function.h"

using namespace llvm;

NAMESPACE_SC_BEGIN

AllocatorInfo::~AllocatorInfo() {}

Value *
SimpleAllocatorInfo::getAllocSize(Value * AllocSite) const {
  CallInst * CI = dyn_cast<CallInst>(AllocSite);
  if (!CI)
    return NULL;

  Function * F  = CI->getCalledFunction();

  if (!F || F->getName() != allocCallName) 
    return NULL;

  return CI->getOperand(allocSizeOperand);
}

Value *
SimpleAllocatorInfo::getFreedPointer(Value * FreeSite) const {
  CallInst * CI = dyn_cast<CallInst>(FreeSite);

  Function * F  = CI->getCalledFunction();

  if (!F || F->getName() != freeCallName) 
    return NULL;

  return CI->getOperand(freePtrOperand);
}

NAMESPACE_SC_END

