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

#include <cstdlib>
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
cl::opt<bool> InjectEasyDPFaults ("inject-easydp", cl::Hidden,
                                  cl::init(false),
                                  cl::desc("Inject Trivial Dangling Pointer Dereferences"));

cl::opt<bool> InjectHardDPFaults ("inject-harddp", cl::Hidden,
                                  cl::init(false),
                                  cl::desc("Inject Non-Trivial Dangling Pointer Dereferences"));

cl::opt<bool> InjectBadSizes ("inject-badsize", cl::Hidden,
                              cl::init(false),
                              cl::desc("Inject Array Allocations of the Wrong Size"));

cl::opt<bool> InjectBadIndices ("inject-badindices", cl::Hidden,
                                cl::init(false),
                                cl::desc("Inject Bad Indices in GEPs"));

cl::opt<int> Seed ("seed", cl::Hidden, cl::init(1),
                   cl::desc("Seed Value for Random Number Generator"));

cl::opt<int> Frequency ("freq", cl::Hidden, cl::init(100),
                        cl::desc("Probability of Inserting a Fault"));

cl::list<std::string> Funcs ("funcs",
                             cl::Hidden,
                             cl::value_desc("list"),
                             cl::CommaSeparated,
                             cl::desc ("List of functions to process"));

namespace {
  ///////////////////////////////////////////////////////////////////////////
  // Pass Statistics
  ///////////////////////////////////////////////////////////////////////////
  STATISTIC (DPFaults,   "Number of Dangling Pointer Faults Injected");
  STATISTIC (BadSizes,   "Number of Bad Allocation Size Faults Injected");
  STATISTIC (BadIndices, "Number of Bad Indexing Faults Injected");

  // Threshold for determining whether a fault will be inserted
  int threshold;
}

///////////////////////////////////////////////////////////////////////////
// Static Functions
///////////////////////////////////////////////////////////////////////////

//
// Function: doFault()
//
// Description:
//  Uses random number generation to determine if a fault should be inserted.
//
// Return Value:
//  true  - A fault should be inserted.
//  false - A fault should not be inserted.
//
// Pre-conditions:
//  1) The random number generator routines should have been seeded.
//  2) The threshold variable should have been calculated.
//
static inline bool
doFault () {
  if (rand() < threshold)
    return true;
  else
    return false;
}

//
// Function: getFunctionList()
//
// Description:
//  Determine which functions should be processed.
//
void
getFunctionList (Module & M, std::vector<Function *> & List) {
  //
  // If no functions were listed on the command line, then process *all*
  // functions within the module.  Otherwise, create a list of only those given
  // on the command line.
  //
  if (Funcs.size() == 0) {
    for (Module::iterator F = M.begin(), E = M.end(); F != E; ++F) {
      List.push_back (F);
    }
  } else {
    for (unsigned index = 0; index < Funcs.size(); ++index) {
      if (Function * F = M.getFunction(Funcs[index]))
        List.push_back (F);
    }
  }

  return;
}

//
// Method: insertEasyDanglingPointers()
//
// Description:
//  Insert dangling pointer dereferences into the code.  This is done by
//  finding load/store instructions and inserting a free on the pointer to
//  ensure the dereference (and all future dereferences) are illegal.
//
// Return value:
//  true  - The module was modified.
//  false - The module was left unmodified.
//
// Notes:
//  This code utilizes DSA to ensure that the pointer can pointer to heap
//  memory (although the pointer is allowed to alias global and stack memory).
//
bool
FaultInjector::insertEasyDanglingPointers (Function & F) {
  //
  // Ensure that we can get analysis information for this function.
  //
  if (!(TDPass->hasDSGraph(F)))
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
        // Skip if we should not insert a fault.
        if (!doFault()) continue;

        new FreeInst (Pointer, I);
        ++DPFaults;
      }
    }
  }

  return (DPFaults > 0);
}

//
// Method: insertHardDanglingPointers()
//
// Description:
//  Insert dangling pointer dereferences into the code.  This is done by
//  finding instructions that store pointers to memory and free'ing those
//  pointers before the store.  Subsequent loads and uses of the pointer will
//  cause a dangling pointer dereference.
//
// Return value:
//  true  - The module was modified.
//  false - The module was left unmodified.
//
// Notes:
//  This code utilizes DSA to ensure that the pointer can pointer to heap
//  memory (although the pointer is allowed to alias global and stack memory).
//
bool
FaultInjector::insertHardDanglingPointers (Function & F) {
  //
  // Ensure that we can get analysis information for this function.
  //
  if (!(TDPass->hasDSGraph(F)))
    return false;

  //
  // Scan through each instruction of the function looking for store
  // instructions that store a pointer to memory.  Free the pointer right
  // before the store instruction.
  //
  DSGraph & DSG = TDPass->getDSGraph(F);
  for (Function::iterator fI = F.begin(), fE = F.end(); fI != fE; ++fI) {
    BasicBlock & BB = *fI;
    for (BasicBlock::iterator bI = BB.begin(), bE = BB.end(); bI != bE; ++bI) {
      Instruction * I = bI;

      //
      // Look to see if there is an instruction that stores a pointer to
      // memory.  If so, then free the pointer before the store.
      //
      if (StoreInst * SI = dyn_cast<StoreInst>(I)) {
        if (isa<PointerType>(SI->getOperand(0)->getType())) {
          Value * Pointer = SI->getOperand(0);

          //
          // Check to ensure that the pointer aliases with the heap.  If so, go
          // ahead and add the free.  Note that we may introduce an invalid
          // free, but we're injecting errors, so I think that's okay.
          //
          DSNode * Node = DSG.getNodeForValue(Pointer).getNode();
          if (Node && (Node->isHeapNode())) {
            // Skip if we should not insert a fault.
            if (!doFault()) continue;

            new FreeInst (Pointer, I);
            ++DPFaults;
          }
        }
      }
    }
  }

  return (DPFaults > 0);
}

//
// Method: insertBadAllocationSizes()
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
FaultInjector::insertBadAllocationSizes  (Function & F) {
  // Worklist of allocation sites to rewrite
  std::vector<AllocationInst * > WorkList;

  for (Function::iterator fI = F.begin(), fE = F.end(); fI != fE; ++fI) {
    BasicBlock & BB = *fI;
    for (BasicBlock::iterator I = BB.begin(), bE = BB.end(); I != bE; ++I) {
      if (AllocationInst * AI = dyn_cast<AllocationInst>(I)) {
        if (AI->isArrayAllocation()) {
          // Skip if we should not insert a fault.
          if (!doFault()) continue;

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

  //
  // Try harder to make bad allocation sizes.
  //
  WorkList.clear();
  for (Function::iterator fI = F.begin(), fE = F.end(); fI != fE; ++fI) {
    BasicBlock & BB = *fI;
    for (BasicBlock::iterator I = BB.begin(), bE = BB.end(); I != bE; ++I) {
      if (AllocationInst * AI = dyn_cast<AllocationInst>(I)) {
        //
        // Determine if this is a data type that we can make smaller.
        //
        if (((TD->getABITypeSize(AI->getAllocatedType())) > 4) && doFault()) {
          WorkList.push_back(AI);
        }
      }
    }
  }

  //
  // Replace these allocations with an allocation of an integer and cast the
  // result back into the appropriate type.
  //
  while (WorkList.size()) {
    AllocationInst * AI = WorkList.back();
    WorkList.pop_back();

    Instruction * NewAlloc = 0;
    if (isa<MallocInst>(AI))
      NewAlloc =  new MallocInst (Type::Int32Ty,
                                  AI->getArraySize(),
                                  AI->getAlignment(),
                                  AI->getName(),
                                  AI);
    else
      NewAlloc =  new AllocaInst (Type::Int32Ty,
                                  AI->getArraySize(),
                                  AI->getAlignment(),
                                  AI->getName(),
                                  AI);

    NewAlloc = castTo (NewAlloc, AI->getType(), "", AI);
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
// Return value:
//  true  - One or more changes were made to the program.
//  false - No changes were made to the program.
//
bool
FaultInjector::insertBadIndexing (Function & F) {
  //
  // Ensure that we can get analysis information for this function.
  //
  if (!(TDPass->hasDSGraph(F)))
    return false;

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
        // Skip if we should not insert a fault.
        if (!doFault()) continue;

        WorkList.push_back (GEP);
      }
    }
  }

  //
  // Get the DSGraph for this function.
  //
  DSGraph & DSG = TDPass->getDSGraph(F);

  //
  // Iterator through the worklist and transform each GEP.
  //
  while (WorkList.size()) {
    GetElementPtrInst * GEP = WorkList.back();
    WorkList.pop_back();

    //
    // Determine how much to index into the first pointer to generate a bounds
    // overflow.  To do this, we'll first consult DSA to see what the largest
    // object size is for objects to which the source pointer can point.
    //
    Value * Pointer = GEP->getPointerOperand();
    DSNode * Node = DSG.getNodeForValue(Pointer).getNode();
    if (!Node) continue;
    unsigned ObjectSize = Node->getSize();
    unsigned DataTypeSize = TD->getABITypeSize(((PointerType *)(Pointer->getType()))->getElementType());

    // The index arguments to the new GEP
    std::vector<Value *> args;

    //
    // Create a copy of the GEP's indices.
    //
    for (User::op_iterator i = GEP->idx_begin(); i != GEP->idx_end(); ++i) {
      if (i == GEP->idx_begin()) {
        args.push_back (ConstantInt::get (Type::Int32Ty, (ObjectSize / DataTypeSize) + 2, true));
      } else {
        args.push_back (*i);
      }
    }

    //
    // Create the new GEP instruction.
    //
    GetElementPtrInst * NewGEP = GetElementPtrInst::Create (Pointer,
                                                            args.begin(),
                                                            args.end(),
                                                            GEP->getName(),
                                                            GEP);
    GEP->replaceAllUsesWith (NewGEP);
    GEP->eraseFromParent();
    ++BadIndices;
  }

  return (BadIndices > 0);
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

  // Get information on the target architecture for this program
  TD     = &getAnalysis<TargetData>();

  // Initialize the random number generator
  srand (Seed);

  // Calculate the threshold for when a fault should be inserted
  threshold = (RAND_MAX / 100 * Frequency);

  // List of functions to process
  std::vector<Function *> FunctionList;

  // Process each function
  getFunctionList (M, FunctionList);
  while (FunctionList.size()) {
    Function * F = FunctionList.back();
    FunctionList.pop_back();

    // Insert dangling pointer errors
    if (InjectEasyDPFaults) modified |= insertEasyDanglingPointers(*F);
    if (InjectHardDPFaults) modified |= insertHardDanglingPointers(*F);

    // Insert bad allocation sizes
    if (InjectBadSizes) modified |= insertBadAllocationSizes (*F);

    // Insert incorrect indices in GEPs
    if (InjectBadIndices) modified |= insertBadIndexing (*F);
  }

  return modified;
}
