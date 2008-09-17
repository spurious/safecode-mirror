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


#include "FaultInjector.h"
#include "dsa/DSGraph.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InstIterator.h"
#include "llvm/ADT/VectorExtras.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Support/Debug.h"

#include <iostream>

using namespace llvm;

char llvm::FaultInjector::ID = 0;

// Register the pass and tell a bad joke all at the same time.
// I know, I know; it's my own darn fault...
RegisterPass<FaultInjector> MyFault ("faultinjector", "Insert Faults");

namespace {
  ///////////////////////////////////////////////////////////////////////////
  // Command line options
  ///////////////////////////////////////////////////////////////////////////
#if 0
  cl::opt<bool> EnableIncompleteChecks  ("enable-incompletechecks", cl::Hidden,
                                cl::init(false),
                                cl::desc("Enable Checks on Incomplete Nodes"));
#endif

  ///////////////////////////////////////////////////////////////////////////
  // Pass Statistics
  ///////////////////////////////////////////////////////////////////////////
  STATISTIC (DPFaults, "Number of Dangling Pointer Faults Injected");
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
  // Get analysis results from DSA.
  TDPass = &getAnalysis<TDDataStructures>();

  for (Module::iterator mI = M.begin(), mE = M.end(); mI != mE; ++mI) {
    // Insert dangling pointer errors
    insertDanglingPointers(*mI);
  }

  return true;
}
