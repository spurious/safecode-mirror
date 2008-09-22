//===- FaultInjector.cpp - Insert faults into programs -----------------------//
// 
//                          The SAFECode Compiler 
//
// This file was developed by the LLVM research group and is distributed under
// the University of Illinois Open Source License. See LICENSE.TXT for details.
// 
//===----------------------------------------------------------------------===//
//
// This file implements a pass that transforms the program to add the following
// kind of faults:
//
//===----------------------------------------------------------------------===//

#define DEBUG_TYPE "FaultInjector"

#include "FaultInjector.h"
#include "dsa/DSGraph.h"
#include "llvm/Constants.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GetElementPtrTypeIterator.h"
#include "SCUtils.h"

#include <iostream>
#include <vector>

using namespace llvm;

char llvm::FaultInjector::ID = 0;

// Register the pass and tell a bad joke all at the same time.
// I know, I know; it's my own darn fault...
RegisterPass<FaultInjector> MyFault ("faultinjector", "Insert Faults");

///////////////////////////////////////////////////////////////////////////
// Command line options
///////////////////////////////////////////////////////////////////////////
cl::opt<bool> InjectDPFaults ("inject-dp", cl::Hidden,
                              cl::init(false),
                              cl::desc("Inject Dangling Pointer Dereferences"));

cl::opt<bool> InjectBadSizes ("inject-badsize", cl::Hidden,
                              cl::init(false),
                              cl::desc("Inject Array Allocations of the Wrong Size"));

cl::opt<bool> InjectBadIndices ("inject-badindices", cl::Hidden,
                              cl::init(false),
                              cl::desc("Inject Bad Indices in GEPs"));

namespace {
  ///////////////////////////////////////////////////////////////////////////
  // Pass Statistics
  ///////////////////////////////////////////////////////////////////////////
  STATISTIC (DPFaults, "Number of Dangling Pointer Faults Injected");
  STATISTIC (BadSizes, "Number of Bad Allocation Size Faults Injected");
}

//
// Method: insertDanglingPointers()
//
// Description:
//  Insert dangling pointer dereferences into the code.
//
// Return value:
//  true  - The module was modified.
//  false - The module was left unmodified.
//
bool
FaultInjector::insertDanglingPointers (Function & F) {
  //
  // Ensure that we can get analysis information for this function.
  //
  if (!(TDPass->hasGraph(F)))
    return false;

  //
  // Scan through each instruction of the function looking for load and store
  // instructions.  Free the pointer right before.
  //
  DSGraph & DSG = TDPass->getDSGraph(F);
  for (Function::iterator fI = F.begin(), fE = F.end(); fI != fE; ++fI) {
    BasicBlock & BB = *fI;
    for (BasicBlock::iterator bI = BB.begin(), bE = BB.end(); bI != bE; ++bI) {
      Instruction * I = bI;

      //
      // Look to see if there is an instruction that uses a pointer.  If so,
      // then free the pointer before the use.
      //
      Value * Pointer = 0;
      if (LoadInst * LI = dyn_cast<LoadInst>(I))
        Pointer = LI->getPointerOperand();
      else if (StoreInst * SI = dyn_cast<StoreInst>(I))
        Pointer = SI->getPointerOperand();
      else
        continue;

      //
      // Check to ensure that this pointer aliases with the heap.  If so, go
      // ahead and add the free.  Note that we may introduce an invalid free,
      // but we're injecting errors, so I think that's okay.
      //
      DSNode * Node = DSG.getNodeForValue(Pointer).getNode();
      if (Node && (Node->isHeapNode())) {
        new FreeInst (Pointer, I);
        ++DPFaults;
      }
    }
  }

  return (DPFaults > 0);
}

//
// Method: addBadAllocationSizes()
//
// Description:
//  This method will look for allocations and change their size to be
//  incorrect.  It does the following:
//    o) Changes the number of array elements allocated by alloca and malloc.
//
// Return value:
//  true  - The module was modified.
//  false - The module was left unmodified.
//
bool
FaultInjector::addBadAllocationSizes  (Function & F) {
  // Worklist of allocation sites to rewrite
  std::vector<AllocationInst * > WorkList;

  for (Function::iterator fI = F.begin(), fE = F.end(); fI != fE; ++fI) {
    BasicBlock & BB = *fI;
    for (BasicBlock::iterator I = BB.begin(), bE = BB.end(); I != bE; ++I) {
      if (AllocationInst * AI = dyn_cast<AllocationInst>(I)) {
        if (AI->isArrayAllocation()) {
          WorkList.push_back(AI);
        }
      }
    }
  }

  while (WorkList.size()) {
    AllocationInst * AI = WorkList.back();
    WorkList.pop_back();

    Instruction * NewAlloc = 0;
    if (isa<MallocInst>(AI))
      NewAlloc =  new MallocInst (AI->getAllocatedType(),
                                  ConstantInt::get(Type::Int32Ty,0),
                                  AI->getAlignment(),
                                  AI->getName(),
                                  AI);
    else
      NewAlloc =  new AllocaInst (AI->getAllocatedType(),
                                  ConstantInt::get(Type::Int32Ty,0),
                                  AI->getAlignment(),
                                  AI->getName(),
                                  AI);

    AI->replaceAllUsesWith (NewAlloc);
    AI->eraseFromParent();
    ++BadSizes;
  }

  return (BadSizes > 0);
}

//
// Methods: insertBadIndexing()
//
// Description:
//  This method modifieds GEP indexing expressions so that their indices are
//  (most likely) below the bounds of the object pointed to by the source
//  pointer.  It does this by modifying the first index to be -1.
//
bool
FaultInjector::insertBadIndexing (Function & F) {
  // Worklist of allocation sites to rewrite
  std::vector<GetElementPtrInst *> WorkList;

  //
  // Find GEP instructions that index into an array.  Add these to the
  // worklist.
  //
  for (Function::iterator fI = F.begin(), fE = F.end(); fI != fE; ++fI) {
    BasicBlock & BB = *fI;
    for (BasicBlock::iterator I = BB.begin(), bE = BB.end(); I != bE; ++I) {
      if (GetElementPtrInst * GEP = dyn_cast<GetElementPtrInst>(I)) {
        WorkList.push_back (GEP);
      }
    }
  }

  //
  // Iterator through the worklist and transform each GEP.
  //
  while (WorkList.size()) {
    GetElementPtrInst * GEP = WorkList.back();
    WorkList.pop_back();

    // The index arguments to the new GEP
    std::vector<Value *> args;

    //
    // Create a copy of the GEP's indices.
    //
    Constant * Intrinsic = F.getParent()->getOrInsertFunction ("llvm.readcyclecounter", Type::Int64Ty, 0);
    for (User::op_iterator i = GEP->idx_begin(); i != GEP->idx_end(); ++i) {
      if (i == GEP->idx_begin()) {
        args.push_back (ConstantInt::get (Type::Int32Ty, -1, true));
      } else {
        args.push_back (*i);
      }
    }

    //
    // Create the new GEP instruction.
    //
    Value * Pointer = GEP->getPointerOperand();
    GetElementPtrInst * NewGEP = GetElementPtrInst::Create (Pointer,
                                                            args.begin(),
                                                            args.end(),
                                                            GEP->getName(),
                                                            GEP);
    GEP->replaceAllUsesWith (NewGEP);
    GEP->eraseFromParent();
  }

  return true;
}

//
// Method: runOnModule()
//
// Description:
//  This is where the pass begin execution.
//
// Return value:
//  true  - The module was modified.
//  false - The module was left unmodified.
//
bool
FaultInjector::runOnModule(Module &M) {
  // Track whether anything has been modified
  bool modified = false;

  // Get analysis results from DSA.
  TDPass = &getAnalysis<TDDataStructures>();

  for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
    // Insert dangling pointer errors
    if (InjectDPFaults) modified |= insertDanglingPointers(*F);

    // Insert bad allocation sizes
    if (InjectBadSizes) modified |= addBadAllocationSizes (*F);

    // Insert incorrect indices in GEPs
    if (InjectBadIndices) modified |= insertBadIndexing (*F);
  }

  return modified;
}
